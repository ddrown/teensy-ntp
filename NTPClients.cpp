#include <Arduino.h>
#include "NTPClock.h"
#include "NTPClients.h"
#include "LeapSeconds.h"
#include "Elapsed.h"

#if LWIP_IPV6
const CLIENT_ADDR_T zero_addr = {0,0,0,0};
#else
const CLIENT_ADDR_T zero_addr = {0};
#endif

NTPClients::NTPClients() {
  for(uint8_t i = 0; i < NUMCLIENTS; i++) {
    CLIENT_ADDR_SET(&clients[i].addr, &zero_addr);
  }
}

void NTPClients::addRx(CLIENT_ADDR_T *addr, uint16_t port, WireNtpTime rx_s, uint32_t rx_subs) {
  // TODO: dealing with large volume addresses
  for(uint8_t i = 0; i < NUMCLIENTS; i++) {
    if(CLIENT_ADDR_CMP(&clients[i].addr, addr) || CLIENT_ADDR_CMP(&clients[i].addr, &zero_addr)) {
      CLIENT_ADDR_SET(&clients[i].addr, addr);
      clients[i].lastPort = port;
      clients[i].rx_s = rx_s;
      clients[i].rx_subs = rx_subs;
      clients[i].tx_s = WireNtpTime(0);
      clients[i].tx_subs = 0;
      return;
    }
  }
}

void NTPClients::addTx(CLIENT_ADDR_T *addr, uint16_t port, WireNtpTime tx_s, uint32_t tx_subs) {
  for(uint8_t i = 0; i < NUMCLIENTS; i++) {
    if(CLIENT_ADDR_CMP(&clients[i].addr, addr) && clients[i].lastPort == port) {
      clients[i].tx_s = tx_s;
      clients[i].tx_subs = tx_subs;
      return;
    }
  }
}

struct client *NTPClients::findClient(CLIENT_ADDR_T *addr, WireNtpTime ts, uint32_t ts_subs) {
  for(uint8_t i = 0; i < NUMCLIENTS; i++) {
    if(CLIENT_ADDR_CMP(&clients[i].addr, addr) && clients[i].rx_s.v == ts.v && clients[i].rx_subs == ts_subs) {
      return &clients[i];
    }
  }

  return NULL;
}

void NTPClients::expireClients() {
  // localClock is TAI-like (see DateTime.h) but rx_s below is stored in
  // real wire format (it's compared against a client-echoed org_time, which
  // is whatever we actually sent) -- convert before comparing, or this
  // expires clients up to ~37s early/late depending on the current leap
  // offset.
  TaiNtpTime sec;
  localClock.getTime(&sec, NULL);
  WireNtpTime nowWire = taiToWireNtp(sec);

  for(uint8_t i = 0; i < NUMCLIENTS; i++) {
    uint32_t age;
    // elapsedWithin(), not raw subtraction: nowWire/rx_s are 32-bit NTP wire
    // seconds, which wrap every ~136 years -- see TODO.md/DONE.md, "NTP
    // timestamp era rollover (Y2036)" for the same wraparound class in a
    // different domain. Straddling that wrap would otherwise make a client
    // that just registered look expired (or a genuinely stale one look
    // fresh) for one 4096s window. A client is fresh if its last rx_s is
    // within [0, 4096] seconds of now; anything else (too old, or nowWire
    // somehow behind rx_s) gets expired.
    if(!CLIENT_ADDR_CMP(&clients[i].addr, &zero_addr) &&
       !elapsedWithin(nowWire.v, clients[i].rx_s.v, 4096, &age)) { // allow a 2^12 poll time
      CLIENT_ADDR_SET(&clients[i].addr, &zero_addr);
      clients[i].rx_s = WireNtpTime(0);
      clients[i].rx_subs = 0;
      clients[i].tx_s = WireNtpTime(0);
      clients[i].tx_subs = 0;
    }
  }
}
