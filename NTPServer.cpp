#include <Arduino.h>
#include "lwip_t41.h"
#include "lwip/udp.h"
#include "NTPClock.h"
#include "NTPServer.h"
#include "NTPClients.h"
#include "platform-clock.h"

#define TS_POS_S 1
#define TS_POS_SUBS 0

// the values below are in 2^32 fractional second units
// adjusting from preamble timestamp to trailer timestamp: 752 bits at 100M
#define RX_TRAILER 32298
// delay between TX software timestamp and udp_sendto sending packet
#define TX_DELAY 16492
// From DP83825I datasheet, page 10
// "Slave RMII Rising edge XI clock with assertion TX_EN to SSD symbol on MDI (100M)"
// 105 ns
#define TX_PHY 451
// "Slave RMII Rising edge XI clock with assertion TX_EN to SSD symbol on MDI (100M)"
// 350ns
#define RX_PHY 1503

void NTPServer::recv(struct pbuf *request_buf, struct pbuf *response_buf, const ip_addr_t *addr, uint16_t port) {
  union {
    uint32_t parts[2];
    uint64_t whole;
  } RXtimestamp, TXtimestamp;
  struct client *interleavedClient;

  // drop too small packets
  if(request_buf->len < sizeof(struct ntp_packet)) {
    return;
  }

  struct ntp_packet *request = (struct ntp_packet *)request_buf->payload;
  struct ntp_packet *response = (struct ntp_packet *)response_buf->payload;

  if(request->version < 2 || request->version > 4) {
    return; // unknown version
  }

  if(request->mode != NTP_MODE_CLIENT) {
    return; // not a client request
  }

  response->mode = NTP_MODE_SERVER;
  response->version = NTP_VERS_4;
  if(reftime == 0 || dispersion.s32 > 0x10000) {
    // no sync or dispersion over 1s
    response->stratum = 16;
    response->ident = 0;
    response->leap = NTP_LEAP_UNSYNC;
  } else {
    response->stratum = 1;
    response->ident = htonl(0x50505300); // "PPS"
    response->leap = NTP_LEAP_NONE; // TODO: no leap second support
  }
  response->poll = request->poll;
  if(response->poll > 12) {
    response->poll = 12;
  }
  response->precision = -24; // 25MHz = 40ns ~ 2^-24
  response->root_delay = 0;
  response->root_delay_fb = 0;
  response->dispersion = htons(dispersion.s16[TS_POS_S]);
  response->dispersion_fb = htons(dispersion.s16[TS_POS_SUBS]);
  response->ref_time = htonl(reftime);
  response->ref_time_fb = 0;

  localClock_->getTime(request_buf->timestamp, &RXtimestamp.parts[TS_POS_S], &RXtimestamp.parts[TS_POS_SUBS]);
  RXtimestamp.whole += RX_TRAILER - RX_PHY;

  response->recv_time = htonl(RXtimestamp.parts[TS_POS_S]);
  response->recv_time_fb = htonl(RXtimestamp.parts[TS_POS_SUBS]);

  lastTxAddr = ntohl(addr->addr);
  lastTxPort = port;

  if(request->org_time != 0) {
    interleavedClient = clientList.findClient(lastTxAddr, ntohl(request->org_time), ntohl(request->org_time_fb));
  } else {
    interleavedClient = NULL;
  }
  if(interleavedClient && interleavedClient->tx_s != 0) {
    // interleaved mode
    response->org_time = request->recv_time;
    response->org_time_fb = request->recv_time_fb;

    TXtimestamp.parts[TS_POS_S] = interleavedClient->tx_s;
    TXtimestamp.parts[TS_POS_SUBS] = interleavedClient->tx_subs;
    TXtimestamp.whole += TX_PHY;

    response->trans_time = htonl(TXtimestamp.parts[TS_POS_S]);
    response->trans_time_fb = htonl(TXtimestamp.parts[TS_POS_SUBS]);
  } else {
    // basic mode
    response->org_time = request->trans_time;
    response->org_time_fb = request->trans_time_fb;

    localClock_->getTime(&TXtimestamp.parts[TS_POS_S], &TXtimestamp.parts[TS_POS_SUBS]);
    TXtimestamp.whole += TX_DELAY + TX_PHY;

    response->trans_time = htonl(TXtimestamp.parts[TS_POS_S]);
    response->trans_time_fb = htonl(TXtimestamp.parts[TS_POS_SUBS]);
  }

  enet_txTimestampNextPacket();
  udp_sendto(ntp_pcb, response_buf, addr, port);

  clientList.addRx(lastTxAddr, lastTxPort, RXtimestamp.parts[TS_POS_S], RXtimestamp.parts[TS_POS_SUBS]);
}

static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
  struct pbuf *response = pbuf_alloc(PBUF_TRANSPORT, sizeof(struct ntp_packet), PBUF_RAM);
  if(!response) {
    pbuf_free(p);
    return;
  }
  server.recv(p, response, addr, port);
  pbuf_free(p);
  pbuf_free(response);
}

NTPServer::NTPServer(NTPClock *localClock) {
  localClock_ = localClock;
  ntp_pcb = NULL;
  dispersion.s32 = 0xffffffff;
  reftime = 0;
  lastTxAddr = 0;
  lastTxPort = 0;
}

void NTPServer::setReftime(uint32_t newRef) {
  reftime = newRef;
}

void NTPServer::setDispersion(uint32_t newDispersion) {
  dispersion.s32 = newDispersion;
}

void NTPServer::addTxTimestamp(uint32_t ts) {
  uint32_t sec, subsec;
  localClock_->getTime(ts, &sec, &subsec);
  clientList.addTx(lastTxAddr, lastTxPort, sec, subsec);
}

static void interrupt_tx_timestamp(uint32_t ts) {
  server.addTxTimestamp(ts);
}

void NTPServer::setup() {
  enet_set_tx_timestamp_callback(&interrupt_tx_timestamp);
  ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
  udp_recv(ntp_pcb, ntp_recv, NULL);
  udp_bind(ntp_pcb, IP_ANY_TYPE, NTP_PORT);
}
