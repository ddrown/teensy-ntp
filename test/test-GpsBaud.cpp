#include <string.h>
#include "test.h"
#include "DateTime.h"
#include "GPS.h"
#include "InputCapture.h"
#include "GpsBaud.h"

// Mock InputCapture, same stand-in test-GPS.cpp uses -- GPS.cpp references
// the global pps via InputCapture::getCaptures(), but the real
// InputCapture.cpp needs real Teensy hardware registers to build.
InputCapture pps;

void InputCapture::newCapture(uint32_t count) {
  captures = captures + 1;
  lastCount = count;
  lastMillis = 1000;
}

InputCapture::InputCapture() {
  lastCount = 0;
  lastMillis = 0;
  captures = 0;
}

void InputCapture::begin() {
}

// Feeds mockMessage's bytes one at a time through the mocked Serial, exactly
// as a real GPS module would arrive character-by-character, then confirms
// detectGpsBaud() recognizes the first candidate as valid and returns
// immediately rather than waiting out the full probe window.
void test_first_candidate_that_validates_wins() {
  const uint32_t candidates[] = {115200, 9600};
  const char *mockMessage = "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n";
  size_t idx = 0;
  size_t len = strlen(mockMessage);
  // The very first available() call is detectGpsBaud()'s "drop whatever's
  // buffered from the previous rate" drain check -- report nothing buffered
  // there (there's no previous rate in this test), so the mock message below
  // is only ever consumed by the probe loop's decode() calls, not drained
  // and discarded before decode() ever sees it.
  size_t availableCalls = 0;

  When(Method(ArduinoFake(), millis)).AlwaysReturn(1000000);
  When(OverloadedMethod(ArduinoFake(Serial), begin, void(unsigned long))).AlwaysReturn();
  When(Method(ArduinoFake(Serial), available)).AlwaysDo([&availableCalls, &idx, len]() -> int {
    availableCalls++;
    if(availableCalls == 1) {
      return 0;
    }
    return idx < len ? 1 : 0;
  });
  When(Method(ArduinoFake(Serial), read)).AlwaysDo([&idx, mockMessage]() -> int {
    return mockMessage[idx++];
  });

  GPSDateTime gps(&Serial);
  uint32_t result = detectGpsBaud(Serial, gps, candidates, 2);

  TEST_ASSERT_EQUAL(115200, result);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_first_candidate_that_validates_wins);
  return UNITY_END();
}
