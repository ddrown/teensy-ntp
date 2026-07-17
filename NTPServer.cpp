#include <Arduino.h>
#include "lwip_t41.h"
#include "lwip/udp.h"
#include "NTPClock.h"
#include "NTPServer.h"
#include "NTPClients.h"
#include "LeapSeconds.h"
#include "NTPResponseFields.h"
#include "platform-clock.h"

#define TS_POS_S 1
#define TS_POS_SUBS 0

void NTPServer::recv(struct pbuf *request_buf, struct pbuf *response_buf, const ip_addr_t *addr, uint16_t port) {
  struct client *interleavedClient;

  // drop too small packets
  if(!ntpRequestLengthIsValid(request_buf->len)) {
    return;
  }

  struct ntp_packet *request = (struct ntp_packet *)request_buf->payload;
  struct ntp_packet *response = (struct ntp_packet *)response_buf->payload;

  if(!ntpRequestVersionAndModeAreValid(request->version, request->mode)) {
    return; // unknown version, or not a client request
  }

  // localClock_/reftime are TAI-like (leap-second-adjusted, see DateTime.h)
  // -- convert back to real NTP wire format before anything below touches
  // the wire or a client-comparable timestamp.
  WireNtpTime wireReftime = taiToWireNtp(reftime);
  NTPResponseHeader header = selectNTPResponseHeader(reftime, dispersion.s32);

  response->mode = NTP_MODE_SERVER;
  response->version = NTP_VERS_4;
  response->stratum = header.stratum;
  response->ident = htonl(header.ident);
  response->leap = header.leap;
  response->poll = clampNTPPoll(request->poll);
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
  NTPWireTimestamp rxWire = ntpWireTimestampFromTai(rxTai, rxFrac, NTP_RX_LATENCY_CORRECTION);

  response->recv_time = htonl(rxWire.seconds.v);
  response->recv_time_fb = htonl(rxWire.fractional);

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
    // addTxTimestamp()), no further domain conversion needed.
    response->org_time = request->recv_time;
    response->org_time_fb = request->recv_time_fb;

    NTPWireTimestamp txWire = ntpWireTimestampFromWire(interleavedClient->tx_s, interleavedClient->tx_subs, NTP_TX_INTERLEAVED_LATENCY_CORRECTION);
    response->trans_time = htonl(txWire.seconds.v);
    response->trans_time_fb = htonl(txWire.fractional);
  } else {
    // basic mode
    response->org_time = request->trans_time;
    response->org_time_fb = request->trans_time_fb;

    TaiNtpTime txTai;
    uint32_t txFrac;
    localClock_->getTime(&txTai, &txFrac);
    NTPWireTimestamp txWire = ntpWireTimestampFromTai(txTai, txFrac, NTP_TX_BASIC_LATENCY_CORRECTION);
    response->trans_time = htonl(txWire.seconds.v);
    response->trans_time_fb = htonl(txWire.fractional);
  }

  enet_txTimestampNextPacket();
  udp_sendto(ntp_pcb, response_buf, addr, port);

  if (!CLIENT_ADDR_CMP(&lastTxAddr, &zero_addr)) {
    clientList.addRx(&lastTxAddr, lastTxPort, rxWire.seconds, rxWire.fractional);
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
