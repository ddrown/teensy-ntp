#include <Arduino.h>
#include <WiFiUdp.h>
#include "NTPClock.h"
#include "NTPServer.h"
#include "platform-clock.h"

void NTPServer::poll() {
  int rec_length;
  IPAddress src;
  uint16_t port;
  uint32_t recvTS, txTS;

  recvTS = COUNTERFUNC();
  rec_length = udp_->parsePacket();
  if(rec_length == 0) {
    return;
  }

  // drop too small packets
  if(rec_length < sizeof(struct ntp_packet)) {
    return;
  }

  port = udp_->remotePort();
  src = udp_->remoteIP();

  udp_->read((unsigned char *)&packetBuffer_, sizeof(struct ntp_packet));
  
  if(packetBuffer_.version < 2 || packetBuffer_.version > 4) {
    return; // unknown version
  }

  if(packetBuffer_.mode != NTP_MODE_CLIENT) {
    return; // not a client request
  }

  packetBuffer_.mode = NTP_MODE_SERVER;
  packetBuffer_.version = NTP_VERS_4;
  packetBuffer_.leap = NTP_LEAP_NONE; // TODO: no leap second support
  packetBuffer_.stratum = 1;
  if(packetBuffer_.poll < 6) {
    packetBuffer_.poll = 6;
  }
  if(packetBuffer_.poll > 12) {
    packetBuffer_.poll = 12;
  }
  packetBuffer_.precision = -26; // 80MHz
  packetBuffer_.root_delay = 0; // TODO
  packetBuffer_.root_delay_fb = 0; // TODO
  packetBuffer_.dispersion = 0; // TODO
  packetBuffer_.dispersion_fb = 0; // TODO
  packetBuffer_.ident = htonl(0x50505300); // "PPS"
  packetBuffer_.ref_time = htonl(localClock_->getReftime());
  packetBuffer_.ref_time_fb = 0;
  packetBuffer_.org_time = packetBuffer_.trans_time;
  packetBuffer_.org_time_fb = packetBuffer_.trans_time_fb;

  if(!localClock_->getTime(recvTS, &packetBuffer_.recv_time, &packetBuffer_.recv_time_fb)) {
    return; // clock not set yet
  }
  packetBuffer_.recv_time = htonl(packetBuffer_.recv_time);
  packetBuffer_.recv_time_fb = htonl(packetBuffer_.recv_time_fb);

  udp_->beginPacket(src, port);

  txTS = COUNTERFUNC();
  if(!localClock_->getTime(txTS, &packetBuffer_.trans_time, &packetBuffer_.trans_time_fb)) {
    udp_->endPacket();
    return; // clock not set yet
  }
  packetBuffer_.trans_time = htonl(packetBuffer_.trans_time);
  packetBuffer_.trans_time_fb = htonl(packetBuffer_.trans_time_fb);
  udp_->write((char *)&packetBuffer_, sizeof(struct ntp_packet));
  udp_->endPacket();
}
