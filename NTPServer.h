#pragma once

#define NTP_LEAP_NONE   0
#define NTP_LEAP_61S    1
#define NTP_LEAP_59S    2
#define NTP_LEAP_UNSYNC 3
#define NTP_VERS_4      4
#define NTP_VERS_3      3
#define NTP_MODE_RSVD   0
#define NTP_MODE_SYMACT 1
#define NTP_MODE_SYMPAS 2
#define NTP_MODE_CLIENT 3
#define NTP_MODE_SERVER 4
#define NTP_MODE_BROADC 5
#define NTP_MODE_CTRL   6
#define NTP_MODE_PRIV   7
#define NTP_PORT        123

extern "C" {
  struct ntp_packet {
    uint8_t mode : 3;
    uint8_t version : 3;
    uint8_t leap : 2;
    uint8_t stratum;
    uint8_t poll;
    int8_t precision;
    uint16_t root_delay;
    uint16_t root_delay_fb;
    uint16_t dispersion;
    uint16_t dispersion_fb;
    uint32_t ident;
    uint32_t ref_time;
    uint32_t ref_time_fb;
    uint32_t org_time;
    uint32_t org_time_fb;
    uint32_t recv_time;
    uint32_t recv_time_fb;
    uint32_t trans_time;
    uint32_t trans_time_fb;
  };
};

class NTPServer {
  public:
    NTPServer(NTPClock *localClock);
    void recv(struct pbuf *request_buf, struct pbuf *response_buf, const ip_addr_t *addr, uint16_t port);
    void setup();
    void setDispersion(uint32_t newDispersion);
    void setReftime(uint32_t newRef);
    void addTxTimestamp(uint32_t ts);

  private:
    NTPClock *localClock_;
    struct udp_pcb *ntp_pcb;
    union {
      uint16_t s16[2];
      uint32_t s32;
    } dispersion;
    uint32_t reftime;
    uint32_t lastTxAddr;
    uint16_t lastTxPort;
};

extern NTPServer server;
