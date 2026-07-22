#pragma once

#include <stdint.h>

#define GPS_BAUD 115200
#define GPS_SERIAL Serial1

// per-device hostnames, looked up by MAC address so the same firmware image
// can be flashed onto any board -- see hostnameForMac() in teensy-ntp.ino.
// boot the device and check the serial log for its MAC to add an entry here;
// boards not listed fall back to a "teensy-xxxxxx" name from their MAC.
struct HostnameEntry {
  uint8_t mac[6];
  const char *hostname;
};

static const HostnameEntry hostnameTable[] = {
  { {0x04, 0xE9, 0xE5, 0x0b, 0xff, 0x31}, "teensy-1" },
  { {0x04, 0xE9, 0xE5, 0x0b, 0xb2, 0x44}, "teensy-2" },
};
