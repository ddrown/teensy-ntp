#include "test.h"
#include "UpdateTimeCore.h"

// No PPS captured yet at all (mirrors updateTime()'s original first early
// return) -- nothing should be fed to discipline/holdover.
void test_no_pps_yet_returns_early_without_touching_discipline() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline discipline(&clock, &pid);
  ClockHoldover holdover(&clock, &pid);

  UpdateTimeOutcome outcome = updateTimeCore(discipline, holdover, pid,
      1000, TaiNtpTime(500000000),
      /*capturedAtMillis=*/12345, /*ppsMillisAtCapture=*/0, /*nowMillis=*/12345);

  TEST_ASSERT_TRUE(outcome.noPpsYet);
  TEST_ASSERT_EQUAL(0, pid.samples());
}

// A lag of more than 950ms between the PPS pulse and the GPS message must be
// rejected before ever reaching ClockDiscipline -- ppsToGPS is still reported
// (for the "LAG" diagnostic), but no sample is fed.
void test_lag_exceeding_tolerance_is_rejected_before_discipline() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline discipline(&clock, &pid);
  ClockHoldover holdover(&clock, &pid);

  UpdateTimeOutcome outcome = updateTimeCore(discipline, holdover, pid,
      1000, TaiNtpTime(500000000),
      /*capturedAtMillis=*/1951, /*ppsMillisAtCapture=*/1000, /*nowMillis=*/1951);

  TEST_ASSERT_FALSE(outcome.noPpsYet);
  TEST_ASSERT_TRUE(outcome.lagRejected);
  TEST_ASSERT_EQUAL(951, outcome.ppsToGPS);
  TEST_ASSERT_EQUAL(0, pid.samples());
}

// Exactly at the 950ms boundary (inclusive) a sample is still trusted and
// reaches ClockDiscipline -- the first-ever sample always sets the clock.
void test_lag_at_tolerance_boundary_feeds_discipline() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline discipline(&clock, &pid);
  ClockHoldover holdover(&clock, &pid);

  UpdateTimeOutcome outcome = updateTimeCore(discipline, holdover, pid,
      1000, TaiNtpTime(500000000),
      /*capturedAtMillis=*/1950, /*ppsMillisAtCapture=*/1000, /*nowMillis=*/1950);

  TEST_ASSERT_FALSE(outcome.noPpsYet);
  TEST_ASSERT_FALSE(outcome.lagRejected);
  TEST_ASSERT_TRUE(outcome.discipline.clockSet);
  TEST_ASSERT_EQUAL(1, pid.samples());
}

// This is the fix from the ClockPID/holdover bench work: a sample arriving
// while holdover is active must reset ClockPID's buffer first, discarding
// stale pre-outage history instead of mixing it with the fresh post-recovery
// sample. See DONE.md, "ClockPID buffer reset across holdover recovery and
// bootstrap-phase leap seconds".
void test_holdover_triggers_pid_reset_before_processing_next_sample() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline discipline(&clock, &pid);
  ClockHoldover holdover(&clock, &pid);

  // Accumulate a few unrelated real samples first (i=0 sets the clock,
  // i=1..3 are ordinary bootstrap-phase accepts).
  for (uint32_t i = 0; i <= 3; i++) {
    updateTimeCore(discipline, holdover, pid, 1000 + i, TaiNtpTime(500000000 + i), 1000, 1000, 1000);
  }
  TEST_ASSERT_EQUAL(4, pid.samples());
  TEST_ASSERT_FALSE(pid.full());

  // Force holdover: nothing received for longer than HOLDOVER_STALE_MS.
  holdover.poll(1000 + HOLDOVER_STALE_MS + 1);
  TEST_ASSERT_TRUE(holdover.inHoldover());

  // A fresh sample arrives -- must reset pid's buffer to just this sample,
  // not append to the 4 accumulated before the outage.
  updateTimeCore(discipline, holdover, pid, 2000, TaiNtpTime(600000000), 5000, 5000, 5000);
  TEST_ASSERT_EQUAL(1, pid.samples());
}

// Characterizes CURRENT (not-yet-fixed) behavior: noteSampleReceived() fires
// even when ClockDiscipline rejects the sample, so holdover's staleness
// timer resets despite nothing actually being trusted. This is the still-open
// item in TODO.md ("MITM bench session follow-up") -- this test documents
// today's behavior, not an endorsement of it; it should flip if that item is
// ever fixed.
void test_rejected_sample_still_resets_holdover_staleness_timer() {
  NTPClock clock;
  ClockPID_c pid;
  ClockDiscipline discipline(&clock, &pid);
  ClockHoldover holdover(&clock, &pid);

  updateTimeCore(discipline, holdover, pid, 1000, TaiNtpTime(500000000), 1000, 1000, 1000);

  // A backwards jump -- always rejected, leap second or not.
  UpdateTimeOutcome outcome = updateTimeCore(discipline, holdover, pid, 2000, TaiNtpTime(499999999), 2000, 2000, 2000);
  TEST_ASSERT_TRUE(outcome.discipline.rejected);

  HoldoverStatus hs = holdover.poll(2000);
  TEST_ASSERT_FALSE(hs.inHoldover);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_no_pps_yet_returns_early_without_touching_discipline);
  RUN_TEST(test_lag_exceeding_tolerance_is_rejected_before_discipline);
  RUN_TEST(test_lag_at_tolerance_boundary_feeds_discipline);
  RUN_TEST(test_holdover_triggers_pid_reset_before_processing_next_sample);
  RUN_TEST(test_rejected_sample_still_resets_holdover_staleness_timer);
  return UNITY_END();
}
