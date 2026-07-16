#include "lwip_t41.h"
#include "lwip/inet.h"
#include "lwip/dhcp.h"
#include "InputCapture.h"
#include "NtpTimestamp.h"
#include "DateTime.h"
#include "GPS.h"
#include "NTPClock.h"
#include "ClockPID.h"
#include "ClockDiscipline.h"
#include "ClockHoldover.h"
#include "Elapsed.h"
#include "NTPServer.h"
#include "NTPClients.h"
#include "platform-clock.h"
#include "WebServer.h"
#include "WebContent.h"

// see the settings file for common settings
#include "settings.h"

GPSDateTime gps(&GPS_SERIAL);
NTPClock localClock;
NTPClients clientList;
InputCapture pps;
elapsedMillis msec, epoll_msec;
TaiNtpTime compileTime;
ClockDiscipline discipline(&localClock, &ClockPID);
ClockHoldover holdover(&localClock, &ClockPID);
NTPServer server(&localClock);

static void netif_status_callback(struct netif *netif) {
  char str1[IP4ADDR_STRLEN_MAX] = "", str2[IP4ADDR_STRLEN_MAX] = "", str3[IP4ADDR_STRLEN_MAX] = "";
  const ip_addr_t *ip;

  ip = netif_ip_addr4(netif);
  ip4addr_ntoa_r(ip_2_ip4(ip), str1, IP4ADDR_STRLEN_MAX);

  ip = netif_ip_netmask4(netif);
  ip4addr_ntoa_r(ip_2_ip4(ip), str2, IP4ADDR_STRLEN_MAX);

  ip = netif_ip_gw4(netif);
  ip4addr_ntoa_r(ip_2_ip4(ip), str3, IP4ADDR_STRLEN_MAX);
  Serial.printf("netif status changed: ip %s, mask %s, gw %s\r\n", str1, str2, str3);

#if LWIP_IPV6
  for(int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
    char str6[IP6ADDR_STRLEN_MAX] = "";
    if (netif_ip6_addr_state(netif, i) != 0) {
      ip = netif_ip_addr6(netif, i);
      ip6addr_ntoa_r(ip_2_ip6(ip), str6, IP6ADDR_STRLEN_MAX);
      Serial.printf("v6: %s state %d\r\n", str6, netif_ip6_addr_state(netif, i));
    }
  }
#endif
}

static void link_status_callback(struct netif *netif) {
  Serial.printf("enet link status: %s\r\n", netif_is_link_up(netif) ? "up" : "down");
  if (netif_is_link_up(netif)) {
    netif_set_up(netif);
    dhcp_start(netif);
#if LWIP_IPV6
    netif_create_ip6_linklocal_address(netif, 1);
    netif_set_ip6_autoconfig_enabled(netif, 1);
#endif
  }
}

void wait_for_serial() {
  // don't wait forever in case usb doesn't come up
  for (int i = 0; i < 20; i++) {
    if (Serial) return;
    delay(100);
  }
}

void setup() {
  Serial.begin(115200);

  wait_for_serial();

  Serial.println("Ethernet 1588 NTP Server");
  Serial.println("------------------------\n");

  DateTime compile = DateTime(__DATE__, __TIME__);

  GPS_SERIAL.begin(GPS_BAUD);

  enet_init(NULL, NULL, NULL);

  netif_set_status_callback(netif_default, netif_status_callback);
  netif_set_link_callback(netif_default, link_status_callback);
  netif_set_hostname(netif_default, DHCP_HOSTNAME);

  pps.begin();
  server.setup();

  compileTime = compile.ntptime();
  // this needs to happen after enet_init, so the 1588 clock is running
  localClock.setTime(COUNTERFUNC(), compileTime);
  // allow for compile timezone to be 14 hours ahead
  compileTime = TaiNtpTime(compileTime.v - 14*60*60);

  webserver.begin();
  webcontent.begin();

  while(GPS_SERIAL.available()) { // throw away all the text received while starting up
    GPS_SERIAL.read();
  }
  msec = 0;
  epoll_msec = 0;
}

void updateTime(TaiNtpTime gpstime) {
  if(gps.ppsMillis() == 0) {
    return;
  }

  // Raw subtraction for display only (webcontent's "PPS to GPS" field) --
  // if PPS has actually stopped for a very long time this can itself wrap,
  // but that's cosmetic; the accept/reject decision below must not rely on
  // it, since a wrapped-around gap could otherwise silently look "fresh"
  // again and pass the lag check. See TODO.md, "existing lag check is
  // wraparound-unsafe for long outages".
  uint32_t ppsToGPS = gps.capturedAt() - gps.ppsMillis();
  webcontent.setPPSData(ppsToGPS, gps.ppsMillis(), gpstime);

  uint32_t validatedLag;
  if(!elapsedWithin(gps.capturedAt(), gps.ppsMillis(), 950, &validatedLag)) { // allow 950ms between PPS and GPS message
    Serial.print("LAG ");
    Serial.print(ppsToGPS);
    Serial.print(" ");
    Serial.print(gps.ppsMillis());
    Serial.print(" ");
    Serial.println(gpstime.v);
    return;
  }

  DisciplineResult r = discipline.process(gps.ppsCounter(), gpstime);
  holdover.noteSampleReceived(millis());
  if(r.clockSet) {
    Serial.print("S "); // clock set message
    Serial.print(r.pps);
    Serial.print(" ");
    Serial.println(r.gpstime.v);
  } else if(r.updated) {
    holdover.noteDispersion(r.dispersion);
    server.setDispersion(r.dispersion);
    server.setReftime(r.gpstime);

    double offsetHuman = r.offset / (double)4294967296.0;
    webcontent.setLocalClock(r.pps, offsetHuman, ClockPID.d(), ClockPID.d_chi(), localClock.getPpb(), gpstime);
    Serial.print(r.pps);
    Serial.print(" ");
    Serial.print(offsetHuman, 9);
    Serial.print(" ");
    Serial.print(ClockPID.d(), 9);
    Serial.print(" ");
    Serial.print(ClockPID.d_chi(), 9);
    Serial.print(" ");
    Serial.print(localClock.getPpb());
    Serial.print(" ");
    Serial.println(r.gpstime.v);
  }
}

static void slower_poll() {
  if (epoll_msec >= 100) {
    // check link state, update dhcp, etc
    enet_poll();

    epoll_msec = 0;
  }

  if(msec >= 1000) {
    TaiNtpTime s;
    uint32_t s_fb;
    // update the local clock's cycle count
    localClock.getTime(COUNTERFUNC(),&s,&s_fb);

    // remove old NTP clients
    clientList.expireClients();

    HoldoverStatus hs = holdover.poll(millis());
    if(hs.inHoldover) {
      server.setDispersion(hs.dispersion);
    }
    webcontent.setHoldover(hs.inHoldover, hs.dispersion, hs.elapsedMs);

    msec = 0;
  }
}

static void gps_serial_poll() {
  if(GPS_SERIAL.available()) {
    if(gps.decode()) {
      TaiNtpTime gpstime = gps.GPSnow().ntptime();
      if(gpstime.v < compileTime.v) {
        Serial.print("B "); // gps clock bad message (for example, on startup before GPS almanac)
        Serial.println(gpstime.v);
      } else {
        updateTime(gpstime);
      }
    }
  }
}

// useful when using teensy_loader_cli
static void bootloader_poll() {
  if(Serial.available()) {
    char r = Serial.read();
    if(r == 'r') {
      Serial.println("rebooting to bootloader");
      delay(10);
      asm("bkpt #251"); // run bootloader
    }
  }
}

void loop() {
  enet_proc_input();

  slower_poll();

  enet_proc_input();

  gps_serial_poll();

  enet_proc_input();

  bootloader_poll();
}
