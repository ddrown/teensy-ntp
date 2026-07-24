#pragma once

#include <stdint.h>

// Looks up mac against settings.h's hostnameTable[]; falls back to a
// "teensy-xxxxxx" name built from the MAC's last 3 bytes if no entry
// matches, so a board that hasn't been added to the table yet still gets a
// distinguishable name instead of DHCP's default. See teensy-ntp.ino's
// setup(), which feeds this into netif_set_hostname().
const char *hostnameForMac(const uint8_t *mac);
