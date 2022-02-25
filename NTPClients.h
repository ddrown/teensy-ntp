#pragma once
#include "lwip/inet.h"

#if LWIP_IPV6
#define CLIENT_ADDR_T ip6_addr_t
#define CLIENT_ADDR_CMP ip6_addr_cmp
#define CLIENT_ADDR_SET ip6_addr_set
#else
#define CLIENT_ADDR_T ip4_addr_t
#define CLIENT_ADDR_CMP ip4_addr_cmp
#define CLIENT_ADDR_SET ip4_addr_set
#endif

extern const CLIENT_ADDR_T zero_addr;

struct client {
  CLIENT_ADDR_T addr;
  uint16_t lastPort;
  uint32_t rx_s;
  uint32_t rx_subs;
  uint32_t tx_s;
  uint32_t tx_subs;
};

// 100 clients = 2.4kb ram
#define NUMCLIENTS 100

class NTPClients {
  public:
    NTPClients();
    void addRx(CLIENT_ADDR_T *addr, uint16_t port, uint32_t rx_s, uint32_t rx_subs);
    void addTx(CLIENT_ADDR_T *addr, uint16_t port, uint32_t tx_s, uint32_t tx_subs);
    struct client *findClient(CLIENT_ADDR_T *addr, uint32_t ts, uint32_t ts_subs);
    void expireClients();

  private:
    struct client clients[NUMCLIENTS];
};

extern NTPClients clientList;
