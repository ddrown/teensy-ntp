#include "test.h"
#include "DateTime.h"
#include "GPS.h"

#define MOCK_MILLIS 1000000

void test_decode() {
  char mockMessage[]  = "$GPZDA,031659.000,17,12,2019,00,00*51\r\n";
  char mockMessage2[] = "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n";
  char mockMessage3[] = "$GNZDA,031700.000,17,12,2019,00,00*42\n";
  GPSDateTime gps(&Serial);

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage[i]);
    if(gps.decode()) {
      // marked at \r
      TEST_ASSERT_EQUAL(strlen(mockMessage)-2, i);
    }
  }

  DateTime decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552619, decoded.unixtime());

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage2); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage2[i]);
    if(gps.decode()) {
      // marked at \r
      TEST_ASSERT_EQUAL(strlen(mockMessage2)-2, i);
    }
  }

  decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552620, decoded.unixtime());

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage3); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage3[i]);
    if(gps.decode()) {
      // marked at \n
      TEST_ASSERT_EQUAL(strlen(mockMessage3)-1, i);
    }
  }

  decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552620, decoded.unixtime());
  TEST_ASSERT_EQUAL(MOCK_MILLIS, gps.capturedAt());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decode);
  return UNITY_END();
}
