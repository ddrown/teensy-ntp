#pragma once
#include "NTPClients.h"
#include "NtpTimestamp.h"
#include "NTPPacket.h"

class NTPServer {
  public:
    NTPServer(NTPClock *localClock);
    void recv(struct pbuf *request_buf, struct pbuf *response_buf, const ip_addr_t *addr, uint16_t port);
    void setup();
    void setDispersion(uint32_t newDispersion);
    void setReftime(TaiNtpTime newRef);
    void addTxTimestamp(uint32_t ts);

  private:
    NTPClock *localClock_;
    struct udp_pcb *ntp_pcb;
    union {
      uint16_t s16[2];
      uint32_t s32;
    } dispersion;
    TaiNtpTime reftime;
    CLIENT_ADDR_T lastTxAddr;
    uint16_t lastTxPort;
};

extern NTPServer server;
