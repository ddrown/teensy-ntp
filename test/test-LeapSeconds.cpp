#include "test.h"
#include "LeapSeconds.h"
#include "DateTime.h"

// Cross-check the table's hand-computed NTP timestamps against DateTime's
// independently-implemented calendar math -- both are meant to represent
// the same NTP (seconds-since-1900) convention, so they should agree.
// Only checkable for entries within DateTime's documented valid range
// (date2days() is "valid for 2001..2178", DateTime.cpp:11) -- the table's
// 1972 baseline entry predates that and can't be cross-checked this way.
void test_table_timestamps_agree_with_datetime() {
  DateTime d2017 = DateTime(2017, 1, 1, 0, 0, 0);
  TEST_ASSERT_EQUAL(d2017.ntptime(), leapSeconds[leapSecondsCount - 1].effectiveNtpTime);
}

void test_offset_before_first_entry_is_zero() {
  TEST_ASSERT_EQUAL(0, leapSecondOffsetAt(leapSeconds[0].effectiveNtpTime - 1));
}

void test_offset_at_exact_boundary_is_new_value() {
  // 2017-01-01 00:00:00 UTC: the moment the 37s offset takes effect.
  TEST_ASSERT_EQUAL(37, leapSecondOffsetAt(3692217600UL));
}

void test_offset_one_second_before_boundary_is_old_value() {
  // Still the old 36s offset one second earlier.
  TEST_ASSERT_EQUAL(36, leapSecondOffsetAt(3692217600UL - 1));
}

void test_offset_at_last_table_entry_holds_forever_after() {
  // Well after the last known entry -- current codebase has no data past
  // 2017-01-01, so this should just keep returning the last known value.
  TEST_ASSERT_EQUAL(37, leapSecondOffsetAt(3692217600UL + 100000000UL));
}

void test_pending_today_false_well_before_any_entry() {
  LeapSecondType type;
  TEST_ASSERT_FALSE(leapSecondPendingToday(3692217600UL - 200000, &type));
}

void test_pending_today_true_at_start_of_leap_second_day() {
  // 2016-12-31 00:00:00 UTC -- the first moment of the UTC day the leap
  // second occurs.
  LeapSecondType type;
  TEST_ASSERT_TRUE(leapSecondPendingToday(3692217600UL - 86400, &type));
  TEST_ASSERT_EQUAL(LEAP_INSERT, type);
}

void test_pending_today_true_at_last_second_of_leap_second_day() {
  LeapSecondType type;
  TEST_ASSERT_TRUE(leapSecondPendingToday(3692217600UL - 1, &type));
  TEST_ASSERT_EQUAL(LEAP_INSERT, type);
}

void test_pending_today_false_at_effective_moment() {
  // The leap second has already happened by the instant the new offset
  // takes effect -- must not still report "pending".
  LeapSecondType type;
  TEST_ASSERT_FALSE(leapSecondPendingToday(3692217600UL, &type));
}

void test_pending_today_false_well_after() {
  LeapSecondType type;
  TEST_ASSERT_FALSE(leapSecondPendingToday(3692217600UL + 200000, &type));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_table_timestamps_agree_with_datetime);
  RUN_TEST(test_offset_before_first_entry_is_zero);
  RUN_TEST(test_offset_at_exact_boundary_is_new_value);
  RUN_TEST(test_offset_one_second_before_boundary_is_old_value);
  RUN_TEST(test_offset_at_last_table_entry_holds_forever_after);
  RUN_TEST(test_pending_today_false_well_before_any_entry);
  RUN_TEST(test_pending_today_true_at_start_of_leap_second_day);
  RUN_TEST(test_pending_today_true_at_last_second_of_leap_second_day);
  RUN_TEST(test_pending_today_false_at_effective_moment);
  RUN_TEST(test_pending_today_false_well_after);
  return UNITY_END();
}
