#pragma once

#include <stdint.h>

// Minimal stand-in for real lwIP's <lwip/ip4_addr.h> (transitively included
// via <lwip/inet.h> in the real stack) -- not a general lwIP mock, just
// enough of ip4_addr_t's shape and the two operations NTPClients.h aliases
// via CLIENT_ADDR_T/CLIENT_ADDR_CMP/CLIENT_ADDR_SET (see NTPClients.h) to
// let NTPClients.cpp compile and behave correctly on host. LWIP_IPV6 is
// never defined in this test build (it comes from the real lwipopts.h,
// part of the external teensy41_ethernet library this repo doesn't vendor),
// so NTPClients.h's "#if LWIP_IPV6" always takes the ip4 branch here, same
// as it does today for every other file built without that library. See
// CLAUDE.md, "Build / test commands".

typedef struct {
  uint32_t addr;
} ip4_addr_t;

static inline void ip4_addr_set(ip4_addr_t *dest, const ip4_addr_t *src) {
  dest->addr = (src == NULL) ? 0 : src->addr;
}

static inline int ip4_addr_cmp(const ip4_addr_t *addr1, const ip4_addr_t *addr2) {
  return addr1->addr == addr2->addr;
}
