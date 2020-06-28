#pragma once

// TODO: ipv6 support
struct client {
  uint32_t addr;
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
    void addRx(uint32_t addr, uint16_t port, uint32_t rx_s, uint32_t rx_subs);
    void addTx(uint32_t addr, uint16_t port, uint32_t tx_s, uint32_t tx_subs);
    struct client *findClient(uint32_t addr, uint32_t ts, uint32_t ts_subs);
    void expireClients();

  private:
    struct client clients[NUMCLIENTS];
};

extern NTPClients clientList;
