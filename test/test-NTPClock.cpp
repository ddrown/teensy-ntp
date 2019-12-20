#include "test.h"
#include <time.h>
#include "NTPClock.h"

void test_settime() {
  NTPClock clock;
  uint32_t sec = 0, fractional = 0;

  clock.setTime(0, 12345678);
  When(Method(ArduinoFake(), micros)).Return(1000000);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12345679, sec);
  TEST_ASSERT_EQUAL(0, fractional);
}

void test_gettime() {
  NTPClock clock;
  uint32_t sec = 0, fractional = 0;

  When(Method(ArduinoFake(), micros)).Return(0);
  TEST_ASSERT_EQUAL(0, clock.getTime(&sec, &fractional));

  clock.setTime(0, 12345678);
  When(Method(ArduinoFake(), micros)).Return(1000001);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12345679, sec);
  TEST_ASSERT_EQUAL(4294, fractional);

  When(Method(ArduinoFake(), micros)).Return(1999999);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12345679, sec);
  // 999999/1000000*2^32 = 4294963001
  TEST_ASSERT_EQUAL(4294963001, fractional);

  When(Method(ArduinoFake(), micros)).Return(4000999999);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12349678, sec);
  // 999999/1000000*2^32 = 4294963001
  TEST_ASSERT_EQUAL(4294963001, fractional);

  When(Method(ArduinoFake(), micros)).Return(4293999999);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12349971, sec);
  // 999999/1000000*2^32 = 4294963001
  TEST_ASSERT_EQUAL(4294963001, fractional);
}

void test_wraptime() {
  NTPClock clock;
  uint32_t sec = 0, fractional = 0;

  clock.setTime(4293000000, 12349971);

  When(Method(ArduinoFake(), micros)).Return(32703); // 2^32 wrap by 1s
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12349972, sec);
  TEST_ASSERT_EQUAL(4294963001, fractional);
}

int64_t timespec_nsec(struct timespec *before, struct timespec *after) {
  int64_t ns = 0;
  int32_t s = after->tv_sec - before->tv_sec;
  ns = s * 1000000000;
  ns += after->tv_nsec - before->tv_nsec;
  return ns;
}

void test_setppb() {
  NTPClock clock;
  uint32_t sec = 0, fractional = 0;

  clock.setTime(0, 12345678);
  clock.setPpb(1000);
  When(Method(ArduinoFake(), micros)).Return(1000001);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12345679, sec);
  // 1000001*1.000001 = 1000002
  // 1000002/1000000*2^32 = 4294975885
  // 4294975885-2^32 = 8589, rounding error
  TEST_ASSERT_EQUAL(8588, fractional);

  clock.setTime(0, 12345680);
  clock.setPpb(10000);
  When(Method(ArduinoFake(), micros)).Return(900000);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12345680, sec);
  // 900000*1.00001 = 900009 us
  // 900009/1000000*2^32 = 3865509221, rounding error
  TEST_ASSERT_EQUAL(3865509220, fractional);

  clock.setTime(0, 12345676);
  clock.setPpb(-10000);
  When(Method(ArduinoFake(), micros)).Return(900000);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  TEST_ASSERT_EQUAL(12345676, sec);
  // 900000*(1-0.00001) = 899991
  // 899991/1000000*2^32 = 3865431911, rounding error
  TEST_ASSERT_EQUAL(3865431912, fractional);

  When(Method(ArduinoFake(), micros)).Return(4293999999);
  TEST_ASSERT_EQUAL(1, clock.getTime(&sec, &fractional));
  // 4293999999*(1-0.00001) = 4293957059
  // 4293957059 / 1000000 = 4293 seconds
  // 4293+12345676 = 12349969
  TEST_ASSERT_EQUAL(12349969, sec);
  // 4293957059 % 1000000 = 957059 us
  // 957059/1000000*2^32 = 4110537105, rounding error
  TEST_ASSERT_EQUAL(4110537106, fractional);

  struct timespec before, after;
  clock_gettime(CLOCK_REALTIME, &before);
  clock.getTime(4294000000, &sec, &fractional);
  clock_gettime(CLOCK_REALTIME, &after);
  // takes around 230ns-440ns on an i3-540
  printf("getTime: %09ld ns\n", timespec_nsec(&before, &after));
}

void test_realvalues() {
  NTPClock clock;
  uint32_t sec = 0, fractional = 0;

  clock.setPpb(-668);
  clock.setTime(838698, 3785790043);

  // 120838782-838698= 120000084
  // 120000084 * (1-0.000000668) = 120000003.839943888
  // 120000003.839943888 * 2^32 / 1000000 = 515396092012
  // 515396092012 / 2^32 = 120
  // 3785790043 + 120 = 3785790163
  // 515396092012 % 2^32 = 16492, rounding error
  TEST_ASSERT_EQUAL(1, clock.getTime(120838782, &sec, &fractional));
  TEST_ASSERT_EQUAL(3785790163, sec);
  TEST_ASSERT_EQUAL(16493, fractional);

  // 121838782 - 120838782 = 1000000
  // 1000000 * (1-0.000000668) = 999999.332000000
  // 999999.332000000 * 2^32 / 1000000 = 4294964426
  // 4294964426+16493 = 4294980919
  // 4294980919 / 2^32 = 1
  // 4294980919 % 2^32 = 13623, rounding error
  TEST_ASSERT_EQUAL(1, clock.getTime(121838782, &sec, &fractional));
  TEST_ASSERT_EQUAL(3785790164, sec);
  TEST_ASSERT_EQUAL(13624, fractional);

  // 220838856 - 121838782 = 99000074
  // 99000074 * (1-0.000000668) = 99000007.867950568
  // 99000007.867950568 * 2^32 / 1000000 = 425201796096
  // 425201796096 + 13624 = 425201809720
  // 425201809720 / 2^32 = 99
  // 425201809720 % 2^32 = 47416
  TEST_ASSERT_EQUAL(1, clock.getTime(220838856, &sec, &fractional));
  TEST_ASSERT_EQUAL(3785790263, sec);
  TEST_ASSERT_EQUAL(47416, fractional);

  // 221838856 - 220838856 = 1000000
  // 1000000 * (1-0.000000668) = 999999.332000000
  // 999999.332000000 * 2^32 / 1000000 = 4294964426
  // 4294964426 + 47416 = 4295011842
  // 4295011842 / 2^32 = 1
  // 4295011842 % 2^32 = 44546, rounding error
  TEST_ASSERT_EQUAL(1, clock.getTime(221838856, &sec, &fractional));
  TEST_ASSERT_EQUAL(3785790264, sec);
  TEST_ASSERT_EQUAL(44547, fractional);
}

void test_getoffset() {
  NTPClock clock;

  clock.setPpb(-668);
  clock.setTime(838698, 3785790043);

  TEST_ASSERT_EQUAL(-42949689453LL, clock.getOffset(120838782, 3785790153, 0));
  TEST_ASSERT_EQUAL(-16493, clock.getOffset(120838782, 3785790163, 0));
  TEST_ASSERT_EQUAL(-13624, clock.getOffset(121838782, 3785790164, 0));
  TEST_ASSERT_EQUAL(-47416, clock.getOffset(220838856, 3785790263, 0));
  TEST_ASSERT_EQUAL(-44547, clock.getOffset(221838856, 3785790264, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_settime);
  RUN_TEST(test_gettime);
  RUN_TEST(test_wraptime);
  RUN_TEST(test_setppb);
  RUN_TEST(test_realvalues);
  RUN_TEST(test_getoffset);
  return UNITY_END();
}
