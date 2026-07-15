#include "test.h"
#include "ClockDiscipline.h"
#include "NTPClock.h"
#include "ClockPID.h"

// Fill pid up to ClockPID_c::full() with throwaway samples, independent of
// whatever ClockDiscipline does -- lets tests reach the "full" (steady
// state, resolve-every-3rd-call) branch without depending on the PID math.
static void fillPidFull(ClockPID_c &pid) {
  while (!pid.full()) {
    pid.add_sample(pid.samples() * 1000, pid.samples(), 0);
  }
}

void test_first_sample_sets_clock() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  DisciplineResult r = d.process(1000, 500000000);

  TEST_ASSERT_TRUE(r.clockSet);
  TEST_ASSERT_FALSE(r.updated);
  TEST_ASSERT_EQUAL(1000, r.pps);
  TEST_ASSERT_EQUAL(500000000, r.gpstime);
  TEST_ASSERT_EQUAL(1, pid.samples());
}

// Before the PID has 16 samples, every call resolves immediately (no 3-deep
// median buffering yet) -- this bootstrap-vs-steady-state split previously
// lived un-tested inline in updateTime().
void test_bootstrap_resolves_every_call() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, 500000000); // clock set, pid.samples() == 1

  for (int i = 1; i <= 5; i++) {
    DisciplineResult r = d.process(1000 + i * 1000000, 500000000 + i);
    TEST_ASSERT_FALSE(r.clockSet);
    TEST_ASSERT_TRUE(r.updated);
  }
  TEST_ASSERT_EQUAL(6, pid.samples());
}

// Once the PID is full, a resolve only happens every 3rd call (the other two
// buffer silently) -- this is the "(2+1)*16=48s" cadence referenced in the
// original comment, also previously untested.
void test_full_pid_buffers_two_then_resolves() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, 500000000); // clock set
  fillPidFull(pid);
  TEST_ASSERT_TRUE(pid.full());

  DisciplineResult r1 = d.process(1000, 500000010);
  TEST_ASSERT_FALSE(r1.updated);
  DisciplineResult r2 = d.process(1000, 500000020);
  TEST_ASSERT_FALSE(r2.updated);
  DisciplineResult r3 = d.process(1000, 500000030);
  TEST_ASSERT_TRUE(r3.updated);
}

// Once full, the resolving call picks the *median* offset of the last 3
// samples, not just the latest one -- exercise a couple of arrival orders to
// cover the median() truth table that used to be entirely unverified.
void test_full_pid_selects_median_ascending_order() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, 1000000000); // clock set: lastMicros_ == 1000, ntpTimestamp_ == 1000000000
  fillPidFull(pid);

  // Reuse pps==1000 every call so getOffset()'s elapsed-time term is zero and
  // the resulting offset is purely proportional to (gpstime - 1000000000);
  // arrival order 10, 30, 20 -> median by value is 20.
  d.process(1000, 1000000000 + 10);
  d.process(1000, 1000000000 + 30);
  DisciplineResult r = d.process(1000, 1000000000 + 20);

  TEST_ASSERT_TRUE(r.updated);
  TEST_ASSERT_EQUAL(1000000000 + 20, r.gpstime);
  int64_t expectedOffset = 20LL * 4294967296LL;
  TEST_ASSERT_EQUAL(expectedOffset, r.offset);
  TEST_ASSERT_EQUAL((uint32_t)(expectedOffset >> 16), r.dispersion);
}

void test_full_pid_selects_median_descending_order() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, 1000000000);
  fillPidFull(pid);

  // arrival order 30, 10, 20 -> median by value is still 20.
  d.process(1000, 1000000000 + 30);
  d.process(1000, 1000000000 + 10);
  DisciplineResult r = d.process(1000, 1000000000 + 20);

  TEST_ASSERT_TRUE(r.updated);
  TEST_ASSERT_EQUAL(1000000000 + 20, r.gpstime);
}

// A resolving call must push its ppb/reftime out to localClock, and its
// dispersion out to the caller -- confirms the extraction wired these calls
// through correctly rather than just moving the median/full-buffer logic.
void test_resolve_updates_local_clock() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, 1000000000);
  fillPidFull(pid);

  d.process(1000, 1000000000 + 5);
  d.process(1000, 1000000000 + 5);
  DisciplineResult r = d.process(1000, 1000000000 + 5);

  TEST_ASSERT_TRUE(r.updated);
  TEST_ASSERT_EQUAL(1000000000 + 5, clock.getReftime());
  TEST_ASSERT_FLOAT_WITHIN(1e-6, pid.out() * 1000000000.0, r.ppb);
  TEST_ASSERT_EQUAL((int32_t)r.ppb, clock.getPpb());
}

// The two buffering calls before a resolve must not touch localClock at all
// -- otherwise a partially-filled median window could leak a stale/partial
// correction out early.
void test_buffering_calls_do_not_touch_local_clock() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, 1000000000);
  fillPidFull(pid);
  TEST_ASSERT_EQUAL(0, clock.getPpb());
  TEST_ASSERT_EQUAL(0, clock.getReftime());

  d.process(1000, 1000000000 + 5);
  TEST_ASSERT_EQUAL(0, clock.getPpb());
  TEST_ASSERT_EQUAL(0, clock.getReftime());

  d.process(1000, 1000000000 + 5);
  TEST_ASSERT_EQUAL(0, clock.getPpb());
  TEST_ASSERT_EQUAL(0, clock.getReftime());
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_first_sample_sets_clock);
  RUN_TEST(test_bootstrap_resolves_every_call);
  RUN_TEST(test_full_pid_buffers_two_then_resolves);
  RUN_TEST(test_full_pid_selects_median_ascending_order);
  RUN_TEST(test_full_pid_selects_median_descending_order);
  RUN_TEST(test_resolve_updates_local_clock);
  RUN_TEST(test_buffering_calls_do_not_touch_local_clock);
  return UNITY_END();
}
