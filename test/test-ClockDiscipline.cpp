#include "test.h"
#include "ClockDiscipline.h"
#include "NTPClock.h"
#include "ClockPID.h"

// Fill pid up to ClockPID_c::full() with throwaway samples, independent of
// whatever ClockDiscipline does -- lets tests reach the "full" (steady
// state, resolve-every-3rd-call) branch without depending on the PID math.
static void fillPidFull(ClockPID_c &pid) {
  while (!pid.full()) {
    pid.add_sample(pid.samples() * 1000, TaiNtpTime(pid.samples()), 0);
  }
}

void test_first_sample_sets_clock() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  DisciplineResult r = d.process(1000, TaiNtpTime(500000000));

  TEST_ASSERT_TRUE(r.clockSet);
  TEST_ASSERT_FALSE(r.updated);
  TEST_ASSERT_EQUAL(1000, r.pps);
  TEST_ASSERT_EQUAL(500000000, r.gpstime.v);
  TEST_ASSERT_EQUAL(1, pid.samples());
}

// Before the PID has 16 samples, every call resolves immediately (no 3-deep
// median buffering yet) -- this bootstrap-vs-steady-state split previously
// lived un-tested inline in updateTime().
void test_bootstrap_resolves_every_call() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, TaiNtpTime(500000000)); // clock set, pid.samples() == 1

  for (int i = 1; i <= 5; i++) {
    DisciplineResult r = d.process(1000 + i * 1000000, TaiNtpTime(500000000 + i));
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

  d.process(1000, TaiNtpTime(500000000)); // clock set
  fillPidFull(pid);
  TEST_ASSERT_TRUE(pid.full());

  DisciplineResult r1 = d.process(1000, TaiNtpTime(500000010));
  TEST_ASSERT_FALSE(r1.updated);
  DisciplineResult r2 = d.process(1000, TaiNtpTime(500000020));
  TEST_ASSERT_FALSE(r2.updated);
  DisciplineResult r3 = d.process(1000, TaiNtpTime(500000030));
  TEST_ASSERT_TRUE(r3.updated);
}

// Once full, the resolving call picks the *median* offset of the last 3
// samples, not just the latest one -- exercise a couple of arrival orders to
// cover the median() truth table that used to be entirely unverified.
//
// gpstime must be strictly increasing call-to-call now (see
// ClockDiscipline::process()'s duplicate/backwards guard, TODO.md "Leap
// second handling"), so these two tests can no longer vary *gpstime* out of
// order to control offset arrival order the way they originally did --
// instead pps (hardware ticks) is chosen per call so the local clock's own
// extrapolated elapsed time produces the desired out-of-order *offset*
// sequence while gpstime itself still advances by exactly 1 real second
// each call, same as real GPS/PPS data. Values below were derived and
// cross-checked with a small script mirroring NTPClock::getTime()/
// getOffset() exactly, not hand-approximated.
void test_full_pid_selects_median_ascending_order() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, TaiNtpTime(1000000000)); // clock set: lastMicros_ == 1000, ntpTimestamp_ == 1000000000
  fillPidFull(pid);

  // offset arrival order low (429496730), high (8589505096), mid
  // (4294967296) -- median by value is the 3rd call's own offset/gpstime.
  d.process(901000, TaiNtpTime(1000000001));
  d.process(1100, TaiNtpTime(1000000002));
  DisciplineResult r = d.process(2001000, TaiNtpTime(1000000003));

  TEST_ASSERT_TRUE(r.updated);
  TEST_ASSERT_EQUAL(1000000003, r.gpstime.v);
  TEST_ASSERT_EQUAL(4294967296LL, r.offset);
  TEST_ASSERT_EQUAL((uint32_t)(4294967296LL >> 16), r.dispersion);
}

void test_full_pid_selects_median_descending_order() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, TaiNtpTime(1000000000));
  fillPidFull(pid);

  // offset arrival order high (2147483648), low (-6442450944), mid
  // (-4294967296) -- median by value is again the 3rd call's own value.
  d.process(501000, TaiNtpTime(1000000001));
  d.process(3501000, TaiNtpTime(1000000002));
  DisciplineResult r = d.process(4001000, TaiNtpTime(1000000003));

  TEST_ASSERT_TRUE(r.updated);
  TEST_ASSERT_EQUAL(1000000003, r.gpstime.v);
  TEST_ASSERT_EQUAL(-4294967296LL, r.offset);
}

// A resolving call must push its ppb/reftime out to localClock, and its
// dispersion out to the caller -- confirms the extraction wired these calls
// through correctly rather than just moving the median/full-buffer logic.
void test_resolve_updates_local_clock() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, TaiNtpTime(1000000000));
  fillPidFull(pid);

  // pps fixed at 1000 (no elapsed time) so offset is purely proportional to
  // gpstime -- median of (5, 6, 7) by value is 6, the 2nd arrival.
  d.process(1000, TaiNtpTime(1000000005));
  d.process(1000, TaiNtpTime(1000000006));
  DisciplineResult r = d.process(1000, TaiNtpTime(1000000007));

  TEST_ASSERT_TRUE(r.updated);
  TEST_ASSERT_EQUAL(1000000006, clock.getReftime().v);
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

  d.process(1000, TaiNtpTime(1000000000));
  fillPidFull(pid);
  TEST_ASSERT_EQUAL(0, clock.getPpb());
  TEST_ASSERT_EQUAL(0, clock.getReftime().v);

  d.process(1000, TaiNtpTime(1000000005));
  TEST_ASSERT_EQUAL(0, clock.getPpb());
  TEST_ASSERT_EQUAL(0, clock.getReftime().v);

  d.process(1000, TaiNtpTime(1000000006));
  TEST_ASSERT_EQUAL(0, clock.getPpb());
  TEST_ASSERT_EQUAL(0, clock.getReftime().v);
}

// A duplicate gpstime unrelated to a leap second (a GPS glitch, a repeated
// fix) must be rejected outright -- not fed to the PID, not pushed to
// localClock. See TODO.md, "Leap second handling".
void test_duplicate_gpstime_outside_leap_window_is_rejected() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, TaiNtpTime(500000000)); // clock set
  d.process(1000, TaiNtpTime(500000001));
  uint32_t samplesBefore = pid.samples();

  DisciplineResult r = d.process(1000, TaiNtpTime(500000001)); // duplicate
  TEST_ASSERT_TRUE(r.rejected);
  TEST_ASSERT_FALSE(r.clockSet);
  TEST_ASSERT_FALSE(r.updated);
  TEST_ASSERT_FALSE(r.leapSecondCorrected);
  TEST_ASSERT_EQUAL(samplesBefore, pid.samples());
}

// Real GPS receivers have been observed to stall on the last regular second
// instead of emitting a literal :60 -- see TODO.md, "Leap second handling".
// This must be accepted with gpstime stepped forward by 1, not rejected.
void test_duplicate_gpstime_at_leap_stall_is_corrected() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  // 2016-12-31 23:59:59 (TAI-domain) -- the real leap second's last regular
  // second, same value cross-checked in test-LeapSeconds.cpp's
  // test_offset_two_before_tai_boundary_is_old_value_not_leap_instant and
  // test_stall_second_true_at_last_second_of_leap_day (wire 3692217599).
  TaiNtpTime stallSecond(3692217637UL - 2);
  d.process(1000, stallSecond); // clock set
  uint32_t samplesBefore = pid.samples();

  DisciplineResult r = d.process(1000, stallSecond); // receiver stalls, repeats it

  TEST_ASSERT_FALSE(r.rejected);
  TEST_ASSERT_TRUE(r.leapSecondCorrected);
  TEST_ASSERT_EQUAL(stallSecond.v + 1, r.gpstime.v);
  // Bootstrap (pid not yet full): the correction resets pid rather than
  // appending to it (see test_leap_stall_resets_pid_buffer_during_bootstrap
  // for a case where this is unambiguous), so the count here ends up equal
  // to samplesBefore, not greater than it.
  TEST_ASSERT_EQUAL(samplesBefore, pid.samples());
}

// Same correction, but with several *unrelated* real samples already
// accumulated first, so "count ends up equal to before" can't be a
// coincidence of starting from a single sample -- confirms this is a
// genuine reset, discarding prior bootstrap history rather than appending.
// This is the fix for the corruption found on the bench: an anomalous
// leap-corrected sample (its offset is genuinely ~1s off, since nothing
// steps localClock across the inserted second) landing unfiltered in
// ClockPID's regression during bootstrap, since median-of-3 outlier
// rejection isn't active yet at that point -- see TODO.md, "Leap second
// handling".
void test_leap_stall_resets_pid_buffer_during_bootstrap() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  // Lead-in samples must land within the monotonicity guard's forward-gap
  // window (ClockDiscipline.cpp's elapsedWithin() check) of stallSecond
  // rather than at an arbitrary unrelated epoch -- a gap of more than half
  // of 2^32 seconds is indistinguishable from a backwards/wrapped jump and
  // gets rejected, same as it would on real hardware.
  TaiNtpTime stallSecond(3692217637UL - 2);
  uint32_t start = stallSecond.v - 5;
  d.process(1000, TaiNtpTime(start)); // clock set
  for (int i = 1; i <= 4; i++) {
    d.process(1000 + i * 1000000, TaiNtpTime(start + i));
  }
  TEST_ASSERT_EQUAL(5, pid.samples());
  TEST_ASSERT_FALSE(pid.full());

  d.process(2000000, stallSecond); // ordinary sample, becomes lastGpstime_
  DisciplineResult r = d.process(2000000, stallSecond); // receiver stalls, repeats it

  TEST_ASSERT_TRUE(r.leapSecondCorrected);
  TEST_ASSERT_EQUAL(1, pid.samples());
}

// In steady state, the same correction must NOT reset ClockPID's buffer --
// median-of-3 already filters the one anomalous sample from ever reaching
// the regression (confirmed on the bench with a long pre-leap lead-in), so
// resetting here would just needlessly throw away a perfectly good
// accumulated frequency-estimate history for an ordinary leap second.
void test_leap_stall_does_not_reset_pid_buffer_when_full() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  TaiNtpTime stallSecond(3692217637UL - 2);
  d.process(1000, TaiNtpTime(stallSecond.v - 100)); // clock set, well before the leap
  fillPidFull(pid);
  TEST_ASSERT_TRUE(pid.full());

  d.process(2000000, stallSecond); // ordinary sample, becomes lastGpstime_
  DisciplineResult r = d.process(2000000, stallSecond); // receiver stalls, repeats it

  TEST_ASSERT_TRUE(r.leapSecondCorrected);
  TEST_ASSERT_TRUE(pid.full());
}

// A gpstime *less* than the previous one is never valid, even within a
// leap-pending window -- a real leap second only ever produces a stall (the
// same value again), never something that looks backwards (the
// :57-style vendor-bug case from TODO.md). The leap-window check must not
// accidentally forgive this.
void test_backwards_gpstime_rejected_even_in_leap_window() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  TaiNtpTime stallSecond(3692217637UL - 2);
  d.process(1000, stallSecond); // clock set

  DisciplineResult r = d.process(1000, TaiNtpTime(stallSecond.v - 1));
  TEST_ASSERT_TRUE(r.rejected);
  TEST_ASSERT_FALSE(r.leapSecondCorrected);
}

// The Y2036 wraparound bug: a real sample right after TaiNtpTime.v wraps
// (e.g. 0 or 1) must not be misread as "backwards" just because its raw
// uint32_t value is numerically smaller than the pre-wrap lastGpstime_. See
// TODO.md, "Critical, will affect every deployed unit in 2036".
void test_forward_sample_accepted_across_y2036_wraparound() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, TaiNtpTime(0xFFFFFFFEUL)); // clock set, 2s before the wrap
  DisciplineResult r1 = d.process(2000, TaiNtpTime(0xFFFFFFFFUL)); // 1s before
  DisciplineResult r2 = d.process(3000, TaiNtpTime(0)); // wraps to 0
  DisciplineResult r3 = d.process(4000, TaiNtpTime(1)); // ordinary sample past the wrap

  TEST_ASSERT_FALSE(r1.rejected);
  TEST_ASSERT_FALSE(r2.rejected);
  TEST_ASSERT_FALSE(r3.rejected);
}

// A gap of more than half of 2^32 seconds (~68 years) is indistinguishable
// from a wrapped-around backwards jump and must still be rejected -- the
// wraparound fix must not turn the monotonicity guard into a no-op.
void test_implausibly_large_forward_gap_still_rejected() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline d(&clock, &pid);

  d.process(1000, TaiNtpTime(0)); // clock set
  DisciplineResult r = d.process(2000, TaiNtpTime(0x80000000UL)); // exactly half the range forward

  TEST_ASSERT_TRUE(r.rejected);
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
  RUN_TEST(test_duplicate_gpstime_outside_leap_window_is_rejected);
  RUN_TEST(test_duplicate_gpstime_at_leap_stall_is_corrected);
  RUN_TEST(test_leap_stall_resets_pid_buffer_during_bootstrap);
  RUN_TEST(test_leap_stall_does_not_reset_pid_buffer_when_full);
  RUN_TEST(test_backwards_gpstime_rejected_even_in_leap_window);
  RUN_TEST(test_forward_sample_accepted_across_y2036_wraparound);
  RUN_TEST(test_implausibly_large_forward_gap_still_rejected);
  return UNITY_END();
}
