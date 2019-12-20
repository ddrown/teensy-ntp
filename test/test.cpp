#include <ArduinoFake.h>
#include <unity.h>

using namespace fakeit;

void DateTimeTests();
void gpsTests();
void NTPClockTests();
void ClockPIDTests();

void setUp() {
  ArduinoFakeReset();
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  DateTimeTests();
  gpsTests();
  NTPClockTests();
  ClockPIDTests();

  return UNITY_END();
}

