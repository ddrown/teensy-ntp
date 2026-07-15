#include "test.h"
#include "DateTime.h"

void test_ntpdate() {
  DateTime dec2019 = DateTime(3785457371);
  TEST_ASSERT_EQUAL(12, dec2019.month());
  TEST_ASSERT_EQUAL(2019, dec2019.year());

  DateTime feb2036 = DateTime(4294944000);
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
  TEST_ASSERT_EQUAL(1577363696, dec2019.unixtime());

  DateTime feb2036 = DateTime(2036, 2, 2, 0, 0, 0);
  TEST_ASSERT_EQUAL(2, feb2036.month());
  TEST_ASSERT_EQUAL(2, feb2036.day());
  TEST_ASSERT_EQUAL(2036, feb2036.year());
  TEST_ASSERT_EQUAL(2085523200, feb2036.unixtime());
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

// Known limitation, not yet fixed: DateTime::ntptime() ignores leap seconds
// entirely (time2long() is a plain linear day*86400+h*3600+m*60+s sum), so
// a reported ":60" collides with the following day's ":00" -- both compute
// to the exact same NTP-seconds value, even though they're two distinct PPS
// pulses a full second apart. Left in ClockDiscipline's median-of-3/PID
// window, that reads as zero elapsed real-time for one PPS interval's worth
// of hardware-counter ticks and corrupts the drift-rate regression for as
// long as the sample stays in ClockPID's 16-deep window.
//
// See TODO.md, "Leap second handling": the planned fix makes
// ntptime()/unixtime() leap-second-aware (monotonic) via a compiled
// leap-second table. When that lands, this assertion should flip to
// dec31_leap.ntptime() + 1 == jan1.ntptime() instead.
void test_leap_second_ntptime_collides_with_next_day() {
  DateTime dec31_leap = DateTime(2016, 12, 31, 23, 59, 60);
  DateTime jan1 = DateTime(2017, 1, 1, 0, 0, 0);

  TEST_ASSERT_EQUAL(dec31_leap.ntptime(), jan1.ntptime());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ntpdate);
  RUN_TEST(test_stringdate);
  RUN_TEST(test_numberdate);
  RUN_TEST(test_century_not_leap);
  RUN_TEST(test_second_60_does_not_corrupt_fields);
  RUN_TEST(test_leap_second_ntptime_collides_with_next_day);
  return UNITY_END();
}
