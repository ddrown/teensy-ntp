#pragma once

// use the less precise GPRMC message rather than the G*ZDA messages
// enable this for things like the MTK3339/Adafruit Ultimate GPS
// #define GPS_USES_RMC

#define GPS_BAUD 115200
#define GPS_SERIAL Serial1

#define DHCP_HOSTNAME "teensy-1"
