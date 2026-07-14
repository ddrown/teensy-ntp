#include "test.h"
#include "InputCapture.h"

InputCapture pps;

void test_initial_state() {
  InputCapture ic;

  TEST_ASSERT_EQUAL(0, ic.getCount());
  TEST_ASSERT_EQUAL(0, ic.getMillis());
  TEST_ASSERT_EQUAL(0, ic.getCaptures());
}

void test_newCapture() {
  InputCapture ic;

  When(Method(ArduinoFake(), millis)).Return(1234);
  ic.newCapture(100);

  TEST_ASSERT_EQUAL(100, ic.getCount());
  TEST_ASSERT_EQUAL(1234, ic.getMillis());
  TEST_ASSERT_EQUAL(1, ic.getCaptures());

  When(Method(ArduinoFake(), millis)).Return(5678);
  ic.newCapture(200);

  TEST_ASSERT_EQUAL(200, ic.getCount());
  TEST_ASSERT_EQUAL(5678, ic.getMillis());
  TEST_ASSERT_EQUAL(2, ic.getCaptures());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_initial_state);
  RUN_TEST(test_newCapture);
  return UNITY_END();
}
