#include "test.h"
#include "Hostname.h"
#include "settings.h"

// Uses settings.h's real hostnameTable[] entries directly rather than a
// separate test-only table, so this exercises exactly what production ships.

void test_known_mac_returns_its_configured_hostname() {
  TEST_ASSERT_EQUAL_STRING("teensy-1", hostnameForMac(hostnameTable[0].mac));
}

void test_second_known_mac_returns_its_own_hostname_not_the_first() {
  TEST_ASSERT_EQUAL_STRING("teensy-2", hostnameForMac(hostnameTable[1].mac));
}

void test_unknown_mac_falls_back_to_teensy_prefix_from_last_three_bytes() {
  uint8_t unknownMac[6] = {0x00, 0x11, 0x22, 0xab, 0xcd, 0xef};
  TEST_ASSERT_EQUAL_STRING("teensy-abcdef", hostnameForMac(unknownMac));
}

// Distinct from the case above to confirm the fallback zero-pads each byte
// rather than dropping leading zeros (a naive %x-without-width format would
// print "teensy-1a" instead of "teensy-010a" for these bytes).
void test_unknown_mac_fallback_zero_pads_each_byte() {
  uint8_t unknownMac[6] = {0xff, 0xff, 0xff, 0x00, 0x01, 0x0a};
  TEST_ASSERT_EQUAL_STRING("teensy-00010a", hostnameForMac(unknownMac));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_known_mac_returns_its_configured_hostname);
  RUN_TEST(test_second_known_mac_returns_its_own_hostname_not_the_first);
  RUN_TEST(test_unknown_mac_falls_back_to_teensy_prefix_from_last_three_bytes);
  RUN_TEST(test_unknown_mac_fallback_zero_pads_each_byte);
  return UNITY_END();
}
