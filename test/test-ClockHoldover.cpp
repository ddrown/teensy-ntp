#include "test.h"
#include "ClockHoldover.h"
#include "NTPClock.h"
#include "ClockPID.h"

// poll() never having seen a sample (GPS hasn't locked yet at all) must not
// be treated as an outage -- there's nothing to hold over *from*.
void test_no_holdover_before_first_sample() {
  NTPClock clock;
  ClockPID_c pid;
  ClockHoldover h(&clock, &pid);

  HoldoverStatus hs = h.poll(1000000);

  TEST_ASSERT_FALSE(hs.inHoldover);
  TEST_ASSERT_EQUAL(0, clock.getPpb());
}

// Gaps up to and including HOLDOVER_STALE_MS since the last sample are
// still trusted -- exercises the inclusive boundary.
void test_stays_fresh_within_stale_window() {
  NTPClock clock;
  ClockPID_c pid;
  ClockHoldover h(&clock, &pid);

  h.noteSampleReceived(1000, TaiNtpTime(500000000));

  HoldoverStatus hs = h.poll(1000 + HOLDOVER_STALE_MS);
  TEST_ASSERT_FALSE(hs.inHoldover);
  TEST_ASSERT_EQUAL(0, clock.getPpb());
}

// Once a sample is more than HOLDOVER_STALE_MS old, holdover must drive
// localClock's ppb from the D term alone -- not the full P+I+D output --
// otherwise a stale P/I contribution keeps leaking into the correction.
void test_enters_holdover_and_drives_d_only_ppb() {
  NTPClock clock;
  ClockPID_c pid;
  ClockHoldover h(&clock, &pid);

  // Seed the PID with a few samples that give P, I and D all distinct,
  // non-negligible values (same seed shape as test-ClockPID.cpp's
  // test_addsample, which documents d() converging to ~0.000005 by the
  // 3rd sample).
  for (int i = 0; i < 3; i++) {
    pid.add_sample(999995 * i, TaiNtpTime(i), (int64_t)(21474.836480 * i));
  }
  int32_t dOnlyPpb = (int32_t)(pid.d_out() * 1000000000.0);
  int32_t fullPidPpb = (int32_t)(pid.out() * 1000000000.0);
  // sanity check: this seed data must actually distinguish D-only from
  // full P+I+D, otherwise the assertion below can't catch a regression.
  TEST_ASSERT_NOT_EQUAL(fullPidPpb, dOnlyPpb);

  h.noteSampleReceived(1000, TaiNtpTime(500000000));
  HoldoverStatus hs = h.poll(1000 + HOLDOVER_STALE_MS + 1);

  TEST_ASSERT_TRUE(hs.inHoldover);
  TEST_ASSERT_EQUAL(dOnlyPpb, clock.getPpb());
  TEST_ASSERT_TRUE(hs.elapsedMs > 0);
}

// Dispersion should grow monotonically the longer holdover runs, starting
// from the last real (non-holdover) dispersion value.
void test_dispersion_grows_incrementally_while_in_holdover() {
  NTPClock clock;
  ClockPID_c pid;
  ClockHoldover h(&clock, &pid);

  h.noteDispersion(2000);
  h.noteSampleReceived(0, TaiNtpTime(500000000));

  uint32_t now = 0;
  HoldoverStatus prev = {};
  bool sawGrowth = false;
  for (int i = 0; i < 20; i++) {
    now += 1000; // poll roughly once/sec, matching slower_poll()'s cadence
    HoldoverStatus hs = h.poll(now);
    if (hs.inHoldover) {
      if (prev.inHoldover) {
        TEST_ASSERT_TRUE(hs.dispersion >= prev.dispersion);
        TEST_ASSERT_TRUE(hs.elapsedMs >= prev.elapsedMs);
        if (hs.dispersion > prev.dispersion) {
          sawGrowth = true;
        }
      }
      prev = hs;
    }
  }

  TEST_ASSERT_TRUE(sawGrowth);
  TEST_ASSERT_TRUE(prev.dispersion > 2000);
  TEST_ASSERT_TRUE(prev.elapsedMs > 0);
}

// A single abnormally-large gap between poll() calls (a stalled loop(), not
// a real GPS outage) must not be added wholesale to the holdover-duration
// accumulator -- otherwise one hiccup could make dispersion jump far ahead
// of how long GPS has actually been silent.
void test_large_poll_gap_does_not_inflate_dispersion() {
  NTPClock clock;
  ClockPID_c pid;
  ClockHoldover h(&clock, &pid);

  h.noteDispersion(2000);
  h.noteSampleReceived(0, TaiNtpTime(500000000));

  uint32_t now = HOLDOVER_STALE_MS + 1000;
  HoldoverStatus before = h.poll(now);
  TEST_ASSERT_TRUE(before.inHoldover);

  now += HOLDOVER_POLL_MAX_GAP_MS + 10000; // far larger than the trusted per-tick gap
  HoldoverStatus after = h.poll(now);
  TEST_ASSERT_TRUE(after.inHoldover);
  TEST_ASSERT_EQUAL(before.elapsedMs, after.elapsedMs);
  TEST_ASSERT_EQUAL(before.dispersion, after.dispersion);
}

// holdoverStartTime is a fixed UTC timestamp (the last accepted sample's own
// time, plus the HOLDOVER_STALE_MS grace window), not a live-ticking
// duration -- it must read the same value across repeated polls within the
// same holdover episode, and it must correctly account for the grace window
// rather than just echoing the last accepted sample's own timestamp.
void test_holdover_start_time_is_last_good_sample_plus_stale_window() {
  NTPClock clock;
  ClockPID_c pid;
  ClockHoldover h(&clock, &pid);

  h.noteSampleReceived(1000, TaiNtpTime(500000000));

  HoldoverStatus first = h.poll(1000 + HOLDOVER_STALE_MS + 1000);
  TEST_ASSERT_TRUE(first.inHoldover);
  TEST_ASSERT_EQUAL(500000000 + HOLDOVER_STALE_MS / 1000, first.holdoverStartTime.v);

  // Still the same value on a later poll within the same episode.
  HoldoverStatus second = h.poll(1000 + HOLDOVER_STALE_MS + 5000);
  TEST_ASSERT_TRUE(second.inHoldover);
  TEST_ASSERT_EQUAL(first.holdoverStartTime.v, second.holdoverStartTime.v);
}

// A fresh sample arriving mid-holdover must fully reset holdover state:
// leaving holdover immediately, and if it goes stale again later, growing
// from the newly-recorded dispersion rather than continuing where the
// previous holdover episode left off.
void test_fresh_sample_resets_holdover_state() {
  NTPClock clock;
  ClockPID_c pid;
  ClockHoldover h(&clock, &pid);

  h.noteDispersion(2000);
  h.noteSampleReceived(0, TaiNtpTime(500000000));

  uint32_t now = 0;
  HoldoverStatus stale = {};
  for (int i = 0; i < 20; i++) {
    now += 1000;
    stale = h.poll(now);
  }
  TEST_ASSERT_TRUE(stale.inHoldover);
  TEST_ASSERT_TRUE(stale.dispersion > 2000);
  TEST_ASSERT_TRUE(stale.elapsedMs > 5000);

  // GPS resumes: leaves holdover immediately and records a new baseline.
  now += 100;
  h.noteSampleReceived(now, TaiNtpTime(600000000));
  h.noteDispersion(3000);
  HoldoverStatus fresh = h.poll(now);
  TEST_ASSERT_FALSE(fresh.inHoldover);

  // Goes stale again almost immediately (a handful of poll ticks, stopping
  // as soon as it does). If the duration accumulator had *not* been reset,
  // this would inherit ~16s worth of holdover duration from the episode
  // above and grow well past the fresh baseline; it should instead grow by
  // only a tick or two. holdoverStartTime should also reflect the new
  // baseline (600000000), not the previous episode's (500000000).
  HoldoverStatus staleAgain = {};
  for (int i = 0; i < 20 && !staleAgain.inHoldover; i++) {
    now += 1000;
    staleAgain = h.poll(now);
  }
  TEST_ASSERT_TRUE(staleAgain.inHoldover);
  TEST_ASSERT_TRUE(staleAgain.dispersion >= 3000);
  TEST_ASSERT_TRUE(staleAgain.dispersion - 3000 < 5);
  TEST_ASSERT_TRUE(staleAgain.elapsedMs < stale.elapsedMs);
  TEST_ASSERT_EQUAL(600000000 + HOLDOVER_STALE_MS / 1000, staleAgain.holdoverStartTime.v);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_no_holdover_before_first_sample);
  RUN_TEST(test_stays_fresh_within_stale_window);
  RUN_TEST(test_enters_holdover_and_drives_d_only_ppb);
  RUN_TEST(test_dispersion_grows_incrementally_while_in_holdover);
  RUN_TEST(test_large_poll_gap_does_not_inflate_dispersion);
  RUN_TEST(test_holdover_start_time_is_last_good_sample_plus_stale_window);
  RUN_TEST(test_fresh_sample_resets_holdover_state);
  return UNITY_END();
}
