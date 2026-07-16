#include "test.h"
#include "LeapSeconds.h"
#include "DateTime.h"

// Cross-check the table's hand-computed NTP timestamps against DateTime's
// independently-implemented calendar math. DateTime::ntptime() returns a
// TAI-like (leap-second-adjusted) value, not the raw wire-format value the
// table itself is keyed on (see DateTime.h), so the two are only directly
// comparable once converted back to wire format via taiToWireNtp() -- which
// is itself exactly the round-trip this is meant to cross-check. Only
// checkable for entries within DateTime's documented valid range
// (date2days() is "valid for 2001..2178", DateTime.cpp:11) -- the table's
// 1972 baseline entry predates that and can't be cross-checked this way.
void test_table_timestamps_agree_with_datetime() {
  DateTime d2017 = DateTime(2017, 1, 1, 0, 0, 0);
  TEST_ASSERT_EQUAL(leapSeconds[leapSecondsCount - 1].effectiveNtpTime.v, taiToWireNtp(d2017.ntptime()).v);
}

void test_offset_before_first_entry_is_zero() {
  TEST_ASSERT_EQUAL(0, leapSecondOffsetAt(WireNtpTime(leapSeconds[0].effectiveNtpTime.v - 1)));
}

void test_offset_at_exact_boundary_is_new_value() {
  // 2017-01-01 00:00:00 UTC: the moment the 37s offset takes effect.
  TEST_ASSERT_EQUAL(37, leapSecondOffsetAt(WireNtpTime(3692217600UL)));
}

void test_offset_one_second_before_boundary_is_old_value() {
  // Still the old 36s offset one second earlier.
  TEST_ASSERT_EQUAL(36, leapSecondOffsetAt(WireNtpTime(3692217600UL - 1)));
}

void test_offset_at_last_table_entry_holds_forever_after() {
  // Well after the last known entry -- current codebase has no data past
  // 2017-01-01, so this should just keep returning the last known value.
  TEST_ASSERT_EQUAL(37, leapSecondOffsetAt(WireNtpTime(3692217600UL + 100000000UL)));
}

void test_pending_today_false_well_before_any_entry() {
  LeapSecondType type;
  TEST_ASSERT_FALSE(leapSecondPendingToday(WireNtpTime(3692217600UL - 200000), &type));
}

void test_pending_today_true_at_start_of_leap_second_day() {
  // 2016-12-31 00:00:00 UTC -- the first moment of the UTC day the leap
  // second occurs.
  LeapSecondType type;
  TEST_ASSERT_TRUE(leapSecondPendingToday(WireNtpTime(3692217600UL - 86400), &type));
  TEST_ASSERT_EQUAL(LEAP_INSERT, type);
}

void test_pending_today_true_at_last_second_of_leap_second_day() {
  LeapSecondType type;
  TEST_ASSERT_TRUE(leapSecondPendingToday(WireNtpTime(3692217600UL - 1), &type));
  TEST_ASSERT_EQUAL(LEAP_INSERT, type);
}

void test_pending_today_false_at_effective_moment() {
  // The leap second has already happened by the instant the new offset
  // takes effect -- must not still report "pending".
  LeapSecondType type;
  TEST_ASSERT_FALSE(leapSecondPendingToday(WireNtpTime(3692217600UL), &type));
}

void test_pending_today_false_well_after() {
  LeapSecondType type;
  TEST_ASSERT_FALSE(leapSecondPendingToday(WireNtpTime(3692217600UL + 200000), &type));
}

// TAI-domain (reverse-lookup) counterparts of the wire-domain tests above.
// The 2017-01-01 wire boundary (3692217600) plus its own new offset (37)
// gives the TAI-domain boundary: 3692217637.

void test_offset_at_tai_boundary_is_new_value_not_leap_instant() {
  bool isLeapInstant;
  TEST_ASSERT_EQUAL(37, leapSecondOffsetAtTai(TaiNtpTime(3692217637UL), &isLeapInstant));
  TEST_ASSERT_FALSE(isLeapInstant);
}

// One TAI-second before the boundary is the leap second itself (2016-12-31
// 23:59:60 UTC) -- it should report the *old* offset, since that's what
// reconstructs the correct wire-format day, and flag isLeapInstant.
void test_offset_one_before_tai_boundary_is_leap_instant() {
  bool isLeapInstant;
  TEST_ASSERT_EQUAL(36, leapSecondOffsetAtTai(TaiNtpTime(3692217637UL - 1), &isLeapInstant));
  TEST_ASSERT_TRUE(isLeapInstant);
}

void test_offset_two_before_tai_boundary_is_old_value_not_leap_instant() {
  bool isLeapInstant;
  TEST_ASSERT_EQUAL(36, leapSecondOffsetAtTai(TaiNtpTime(3692217637UL - 2), &isLeapInstant));
  TEST_ASSERT_FALSE(isLeapInstant);
}

void test_offset_at_tai_holds_forever_after_last_entry() {
  bool isLeapInstant;
  TEST_ASSERT_EQUAL(37, leapSecondOffsetAtTai(TaiNtpTime(3692217637UL + 100000000UL), &isLeapInstant));
  TEST_ASSERT_FALSE(isLeapInstant);
}

void test_tai_to_wire_ntp_normal_second() {
  // Well clear of any boundary: 2020-06-15ish, offset 37 throughout.
  TEST_ASSERT_EQUAL(3800000000UL, taiToWireNtp(TaiNtpTime(3800000000UL + 37)).v);
}

// Both the leap second itself and the following day's :00 convert back to
// the exact same wire-format value -- that collision is the wire format's
// own inherent limitation (why NTP has the LI field at all), not something
// this conversion can or should hide.
void test_tai_to_wire_ntp_leap_instant_and_next_day_collide_on_wire() {
  TEST_ASSERT_EQUAL(3692217600UL, taiToWireNtp(TaiNtpTime(3692217637UL - 1)).v); // leap instant
  TEST_ASSERT_EQUAL(3692217600UL, taiToWireNtp(TaiNtpTime(3692217637UL)).v);     // next day :00
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
  RUN_TEST(test_offset_at_tai_boundary_is_new_value_not_leap_instant);
  RUN_TEST(test_offset_one_before_tai_boundary_is_leap_instant);
  RUN_TEST(test_offset_two_before_tai_boundary_is_old_value_not_leap_instant);
  RUN_TEST(test_offset_at_tai_holds_forever_after_last_entry);
  RUN_TEST(test_tai_to_wire_ntp_normal_second);
  RUN_TEST(test_tai_to_wire_ntp_leap_instant_and_next_day_collide_on_wire);
  return UNITY_END();
}
