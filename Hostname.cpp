#include <stdio.h>
#include <string.h>
#include "Hostname.h"
#include "settings.h"

const char *hostnameForMac(const uint8_t *mac) {
  for(size_t i = 0; i < sizeof(hostnameTable)/sizeof(hostnameTable[0]); i++) {
    if(memcmp(mac, hostnameTable[i].mac, sizeof(hostnameTable[i].mac)) == 0) {
      return hostnameTable[i].hostname;
    }
  }
  static char fallback[sizeof("teensy-ffffff")];
  snprintf(fallback, sizeof(fallback), "teensy-%02x%02x%02x", mac[3], mac[4], mac[5]);
  return fallback;
}
