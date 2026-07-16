#include <Arduino.h>
#include "lwip_t41.h"
#include "lwip/udp.h"
#include "NTPClock.h"
#include "NTPServer.h"
#include "NTPClients.h"
#include "LeapSeconds.h"
#include "platform-clock.h"

#define TS_POS_S 1
#define TS_POS_SUBS 0

// the values below are in 2^32 fractional second units
// adjusting from preamble timestamp to trailer timestamp: 752 bits at 100M
#define RX_TRAILER 32298
// estimate of delay between TX software timestamp and udp_sendto sending packet, for non-interleaved clients
#define TX_DELAY 16492
// From DP83825I datasheet, page 10
// "Slave RMII Rising edge XI clock with assertion TX_EN to SSD symbol on MDI (100M)"
// 105 ns
#define TX_PHY 451
// "SSD symbol on MDI to Slave RMII Rising edge of XI clock with assertion of CRS_DV (100M)"
// 350ns
#define RX_PHY 1503

void NTPServer::recv(struct pbuf *request_buf, struct pbuf *response_buf, const ip_addr_t *addr, uint16_t port) {
  Ntp64 RXtimestamp, TXtimestamp;
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

  // localClock_/reftime are TAI-like (leap-second-adjusted, see DateTime.h)
  // -- convert back to real NTP wire format before anything below touches
  // the wire or a client-comparable timestamp.
  WireNtpTime wireReftime = taiToWireNtp(reftime);

  response->mode = NTP_MODE_SERVER;
  response->version = NTP_VERS_4;
  if(reftime.v == 0 || dispersion.s32 > 0x10000) {
    // no sync or dispersion over 1s
    response->stratum = 16;
    response->ident = 0;
    response->leap = NTP_LEAP_UNSYNC;
  } else {
    LeapSecondType pendingType;
    response->stratum = 1;
    response->ident = htonl(0x50505300); // "PPS"
    if (leapSecondPendingToday(wireReftime, &pendingType)) {
      response->leap = (pendingType == LEAP_DELETE) ? NTP_LEAP_59S : NTP_LEAP_61S;
    } else {
      response->leap = NTP_LEAP_NONE;
    }
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
  response->ref_time = htonl(wireReftime.v);
  response->ref_time_fb = 0;

  TaiNtpTime rxTai;
  uint32_t rxFrac;
  localClock_->getTime(request_buf->timestamp, &rxTai, &rxFrac);
  RXtimestamp.setSeconds(rxTai.v);
  RXtimestamp.setFractional(rxFrac);
  RXtimestamp.whole += RX_TRAILER - RX_PHY;
  WireNtpTime wireRx = taiToWireNtp(TaiNtpTime(RXtimestamp.seconds()));

  response->recv_time = htonl(wireRx.v);
  response->recv_time_fb = htonl(RXtimestamp.fractional());

#if !LWIP_IPV6
  CLIENT_ADDR_SET(&lastTxAddr, ip_2_ip4(addr));
#else
  if (addr->type == IPADDR_TYPE_V4) {
    lastTxAddr.addr[0] = 0;
    lastTxAddr.addr[1] = 0;
    lastTxAddr.addr[2] = 0;
    lastTxAddr.addr[3] = ip_2_ip4(addr)->addr;
  } else {
    CLIENT_ADDR_SET(&lastTxAddr, ip_2_ip6(addr));
  }
#endif
  lastTxPort = port;

  if(request->org_time != 0 && !CLIENT_ADDR_CMP(&lastTxAddr, &zero_addr)) {
    interleavedClient = clientList.findClient(&lastTxAddr, WireNtpTime(ntohl(request->org_time)), ntohl(request->org_time_fb));
  } else {
    interleavedClient = NULL;
  }
  if(interleavedClient && interleavedClient->tx_s.v != 0) {
    // interleaved mode -- tx_s is already wire format (stored that way by
    // addTxTimestamp()), no further conversion needed.
    response->org_time = request->recv_time;
    response->org_time_fb = request->recv_time_fb;

    TXtimestamp.setSeconds(interleavedClient->tx_s.v);
    TXtimestamp.setFractional(interleavedClient->tx_subs);
    TXtimestamp.whole += TX_PHY;

    response->trans_time = htonl(TXtimestamp.seconds());
    response->trans_time_fb = htonl(TXtimestamp.fractional());
  } else {
    // basic mode
    response->org_time = request->trans_time;
    response->org_time_fb = request->trans_time_fb;

    TaiNtpTime txTai;
    uint32_t txFrac;
    localClock_->getTime(&txTai, &txFrac);
    TXtimestamp.setSeconds(txTai.v);
    TXtimestamp.setFractional(txFrac);
    TXtimestamp.whole += TX_DELAY + TX_PHY;
    WireNtpTime wireTx = taiToWireNtp(TaiNtpTime(TXtimestamp.seconds()));

    response->trans_time = htonl(wireTx.v);
    response->trans_time_fb = htonl(TXtimestamp.fractional());
  }

  enet_txTimestampNextPacket();
  udp_sendto(ntp_pcb, response_buf, addr, port);

  if (!CLIENT_ADDR_CMP(&lastTxAddr, &zero_addr)) {
    clientList.addRx(&lastTxAddr, lastTxPort, wireRx, RXtimestamp.fractional());
  }
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
  reftime = TaiNtpTime(0);
  CLIENT_ADDR_SET(&lastTxAddr, &zero_addr);
  lastTxPort = 0;
}

void NTPServer::setReftime(TaiNtpTime newRef) {
  reftime = newRef;
}

void NTPServer::setDispersion(uint32_t newDispersion) {
  dispersion.s32 = newDispersion;
}

void NTPServer::addTxTimestamp(uint32_t ts) {
  TaiNtpTime sec;
  uint32_t subsec;
  if (!CLIENT_ADDR_CMP(&lastTxAddr, &zero_addr)) {
    localClock_->getTime(ts, &sec, &subsec);
    // Stored for later comparison against a client-echoed org_time, which
    // will be real wire format (whatever we actually sent) -- convert from
    // localClock_'s TAI-like domain now, not at comparison time.
    clientList.addTx(&lastTxAddr, lastTxPort, taiToWireNtp(sec), subsec);
    CLIENT_ADDR_SET(&lastTxAddr, &zero_addr);
  }
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
