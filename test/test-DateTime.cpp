#include "test.h"
#include "DateTime.h"
#include "NtpTimestamp.h"

void test_ntpdate() {
  DateTime dec2019 = DateTime(TaiNtpTime(3785457371));
  TEST_ASSERT_EQUAL(12, dec2019.month());
  TEST_ASSERT_EQUAL(2019, dec2019.year());

  DateTime feb2036 = DateTime(TaiNtpTime(4294944000));
  TEST_ASSERT_EQUAL(2, feb2036.month());
  TEST_ASSERT_EQUAL(2036, feb2036.year());
}

void test_stringdate() {
  DateTime dec2019 = DateTime("Dec 26 2019", "12:34:56");
  TEST_ASSERT_EQUAL(12, dec2019.month());
  TEST_ASSERT_EQUAL(26, dec2019.day());
  TEST_ASSERT_EQUAL(2019, dec2019.year());

  DateTime feb2036 = DateTime("Feb  2 2036", "00:00:00");
  TEST_ASSERT_EQUAL(2, feb2036.month());
  TEST_ASSERT_EQUAL(2, feb2036.day());
  TEST_ASSERT_EQUAL(2036, feb2036.year());
}

void test_numberdate() {
  DateTime dec2019 = DateTime(2019, 12, 26, 12, 34, 56);
  TEST_ASSERT_EQUAL(12, dec2019.month());
  TEST_ASSERT_EQUAL(26, dec2019.day());
  TEST_ASSERT_EQUAL(2019, dec2019.year());
  // +37: unixtime() is TAI-like (leap-second-adjusted, see DateTime.h) --
  // the cumulative offset in effect since 2017-01-01 is 37s.
  TEST_ASSERT_EQUAL(1577363696 + 37, dec2019.unixtime());

  DateTime feb2036 = DateTime(2036, 2, 2, 0, 0, 0);
  TEST_ASSERT_EQUAL(2, feb2036.month());
  TEST_ASSERT_EQUAL(2, feb2036.day());
  TEST_ASSERT_EQUAL(2036, feb2036.year());
  TEST_ASSERT_EQUAL(2085523200 + 37, feb2036.unixtime());
}

// secondstime() epoch is exactly 2000-01-01 00:00:00. 2000 was a leap year,
// so 2001-01-01 is 366 days (31622400s) later.
void test_secondstime_basic() {
  DateTime oneYearIn = DateTime(2001, 1, 1, 0, 0, 0);
  TEST_ASSERT_EQUAL(31622400UL, oneYearIn.secondstime());
}

// ntptime() is deliberately wire-format-constrained and wraps at
// 2036-02-07 (confirmed: 2036-01-01 encodes to 4291747200, just under 2^32;
// 2036-03-01 wraps to 1963904) -- a later date can numerically compare as
// *smaller*, exactly the bug behind "NTP timestamp era rollover (Y2036)" in
// TODO.md/DONE.md. secondstime() has no such constraint and stays correctly
// ordered across the same boundary.
void test_secondstime_monotonic_across_ntp_wire_wrap() {
  DateTime beforeWrap = DateTime(2036, 1, 1, 0, 0, 0);
  DateTime afterWrap = DateTime(2036, 3, 1, 0, 0, 0);

  TEST_ASSERT_TRUE(afterWrap.ntptime().v < beforeWrap.ntptime().v);
  TEST_ASSERT_TRUE(beforeWrap.secondstime() < afterWrap.secondstime());
}

void test_century_not_leap() {
  // 2100 is divisible by 4 and by 100 but not by 400, so it is NOT a leap
  // year. date2days() (encode) and DateTime::time() (decode) must agree,
  // or dates from Mar 2100 onward decode one day short (into a
  // nonexistent Feb 29).
  DateTime mar2100 = DateTime(2100, 3, 1, 0, 0, 0);
  DateTime roundtrip = DateTime(mar2100.ntptime());

  TEST_ASSERT_EQUAL(3, roundtrip.month());
  TEST_ASSERT_EQUAL(1, roundtrip.day());
  TEST_ASSERT_EQUAL(2100, roundtrip.year());
}

// GPS receivers may report a literal ":60" seconds field during a real leap
// second insertion (e.g. 2016-12-31 23:59:60 UTC was a real one). DateTime
// does no bounds-checking on `second` -- confirm that at least doesn't
// corrupt the other fields or crash, independent of whether the resulting
// ntptime() is numerically correct (see the next test -- it isn't yet).
void test_second_60_does_not_corrupt_fields() {
  DateTime leap = DateTime(2016, 12, 31, 23, 59, 60);
  TEST_ASSERT_EQUAL(2016, leap.year());
  TEST_ASSERT_EQUAL(12, leap.month());
  TEST_ASSERT_EQUAL(31, leap.day());
  TEST_ASSERT_EQUAL(23, leap.hour());
  TEST_ASSERT_EQUAL(59, leap.minute());
  TEST_ASSERT_EQUAL(60, leap.second());
}

// Fixed via LeapSeconds.h: ntptime()/unixtime() are now TAI-like (see
// DateTime.h) -- a reported ":60" no longer collides with the following
// day's ":00". Left uncorrected, that aliasing read as zero elapsed
// real-time for one PPS interval's worth of hardware-counter ticks in
// ClockDiscipline's median-of-3/PID window, corrupting the drift-rate
// regression for as long as the sample stayed in ClockPID's 16-deep window.
void test_leap_second_ntptime_is_monotonic_not_aliased() {
  DateTime dec31_leap = DateTime(2016, 12, 31, 23, 59, 60);
  DateTime jan1 = DateTime(2017, 1, 1, 0, 0, 0);

  TEST_ASSERT_EQUAL(dec31_leap.ntptime().v + 1, jan1.ntptime().v);
}

// Round-trip the two instants above back through the TaiNtpTime constructor
// (decode direction) and confirm each reconstructs the correct calendar
// fields -- in particular that the leap second decodes back as a literal
// :60, not as the following day's :00 it aliases with on the wire.
void test_leap_second_ntptime_roundtrips_through_decode() {
  DateTime dec31_leap = DateTime(2016, 12, 31, 23, 59, 60);
  DateTime jan1 = DateTime(2017, 1, 1, 0, 0, 0);

  DateTime decoded_leap = DateTime(dec31_leap.ntptime());
  TEST_ASSERT_EQUAL(2016, decoded_leap.year());
  TEST_ASSERT_EQUAL(12, decoded_leap.month());
  TEST_ASSERT_EQUAL(31, decoded_leap.day());
  TEST_ASSERT_EQUAL(23, decoded_leap.hour());
  TEST_ASSERT_EQUAL(59, decoded_leap.minute());
  TEST_ASSERT_EQUAL(60, decoded_leap.second());

  DateTime decoded_jan1 = DateTime(jan1.ntptime());
  TEST_ASSERT_EQUAL(2017, decoded_jan1.year());
  TEST_ASSERT_EQUAL(1, decoded_jan1.month());
  TEST_ASSERT_EQUAL(1, decoded_jan1.day());
  TEST_ASSERT_EQUAL(0, decoded_jan1.hour());
  TEST_ASSERT_EQUAL(0, decoded_jan1.minute());
  TEST_ASSERT_EQUAL(0, decoded_jan1.second());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ntpdate);
  RUN_TEST(test_stringdate);
  RUN_TEST(test_numberdate);
  RUN_TEST(test_secondstime_basic);
  RUN_TEST(test_secondstime_monotonic_across_ntp_wire_wrap);
  RUN_TEST(test_century_not_leap);
  RUN_TEST(test_second_60_does_not_corrupt_fields);
  RUN_TEST(test_leap_second_ntptime_is_monotonic_not_aliased);
  RUN_TEST(test_leap_second_ntptime_roundtrips_through_decode);
  return UNITY_END();
}
