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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ntpdate);
  RUN_TEST(test_stringdate);
  RUN_TEST(test_numberdate);
  return UNITY_END();
}
