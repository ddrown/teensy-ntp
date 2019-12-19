#include <ArduinoFake.h>
#include <unity.h>

using namespace fakeit;

void DateTimeTests();
void gpsTests();
void NTPClockTests();

void setUp() {
  ArduinoFakeReset();
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  DateTimeTests();
  gpsTests();
  NTPClockTests();

  return UNITY_END();
}

