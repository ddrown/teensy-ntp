#include <ArduinoFake.h>
#include <unity.h>
#include <time.h>

using namespace fakeit;

#include "ClockPID.h"

void test_addsample() {
  ClockPID.reset_clock();
  float expected[NTPPID_MAX_COUNT] = {
    0.000000000,
    0.000005005,
    0.000005011,
    0.000005018,
    0.000005025,
    0.000005032,
    0.000005040,
    0.000005049,
    0.000005058,
    0.000005067,
    0.000005077,
    0.000005088,
    0.000005099,
    0.000005110,
    0.000005122,
    0.000005135
  };

  for(int i = 0; i < NTPPID_MAX_COUNT; i++) {
    // every second, clock drifts 5us and is uncorrected
    float sample = ClockPID.add_sample(1000000*i, 5*i, 21474.836480*i);
    TEST_ASSERT_FLOAT_WITHIN(1e-9, expected[i], sample);
    if(i > 1) {
      TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.000005, ClockPID.d());
      TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, ClockPID.d_chi());
    }
  }
}

void test_realsamples() {
  ClockPID.reset_clock();

  ClockPID.add_sample(838698, 0, 0);

  ClockPID.add_sample(120838782, -84, -16493);
  // -84/(120838782-838698) = -.000000699
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.699e-6, ClockPID.d());

  ClockPID.add_sample(121838782, -84, -13624);
  // -84/(121838782-838698) = -.000000694
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.694e-6, ClockPID.d());

  ClockPID.add_sample(220838856, -158, -47416);
  // -158/(221838856-838698) = -.000000718
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.700e-6, ClockPID.d());

  ClockPID.add_sample(221838856, -158, -44547);
  // -158/(221838856-838698) = -.000000714
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.714e-6, ClockPID.d());
}

void ClockPIDTests() {
  RUN_TEST(test_addsample);
  RUN_TEST(test_realsamples);
}
