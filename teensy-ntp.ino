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
#include "Hostname.h"
#include "BootloaderMagic.h"
#include "GpsBaud.h"
#include "UpdateTimeCore.h"

// see the settings file for common settings
#include "settings.h"

GPSDateTime gps(&GPS_SERIAL);
NTPClock localClock;
NTPClients clientList;
InputCapture pps;
elapsedMillis msec, epoll_msec;
TaiNtpTime compileTime;
// seconds-since-2000 counterpart of compileTime, used only for the
// gpstime/compileTime sanity check in gps_serial_poll() -- see
// DateTime::secondstime() and TODO.md/DONE.md, "NTP timestamp era rollover
// (Y2036)".
uint32_t compileSecondsTime;
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

  uint32_t gpsBaud = detectGpsBaud(GPS_SERIAL, gps, gpsBaudCandidates, sizeof(gpsBaudCandidates)/sizeof(gpsBaudCandidates[0]));
  Serial.print("GPS baud: ");
  Serial.println(gpsBaud);

  enet_init(NULL, NULL, NULL);

  uint8_t mac[6];
  enet_getmac(mac);
  Serial.printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  netif_set_status_callback(netif_default, netif_status_callback);
  netif_set_link_callback(netif_default, link_status_callback);
  netif_set_hostname(netif_default, hostnameForMac(mac));

  pps.begin();
  server.setup();

  compileTime = compile.ntptime();
  compileSecondsTime = compile.secondstime();
  // this needs to happen after enet_init, so the 1588 clock is running
  localClock.setTime(COUNTERFUNC(), compileTime);
  // allow for compile timezone to be 14 hours ahead
  compileSecondsTime -= 14*60*60;

  webserver.begin();
  webcontent.begin();

  while(GPS_SERIAL.available()) { // throw away all the text received while starting up
    GPS_SERIAL.read();
  }
  msec = 0;
  epoll_msec = 0;
}

void updateTime(TaiNtpTime gpstime) {
  UpdateTimeOutcome outcome = updateTimeCore(discipline, holdover, ClockPID,
                                              gps.ppsCounter(), gpstime,
                                              gps.capturedAt(), gps.ppsMillis(), millis());
  if(outcome.noPpsYet) {
    return;
  }

  webcontent.setPPSData(outcome.ppsToGPS, gps.ppsMillis());

  if(outcome.lagRejected) {
    Serial.print("LAG ");
    Serial.print(outcome.ppsToGPS);
    Serial.print(" ");
    Serial.print(gps.ppsMillis());
    Serial.print(" ");
    Serial.println(gpstime.v);
    return;
  }

  DisciplineResult r = outcome.discipline;
  if(r.rejected) {
    // Duplicate (unrelated to a leap second) or backwards GPS timestamp --
    // see TODO.md, "Leap second handling".
    Serial.print("D ");
    Serial.println(gpstime.v);
    return;
  }
  if(r.leapSecondCorrected) {
    // GPS receiver stalled on a second instead of emitting a literal :60 --
    // corrected, not rejected. See TODO.md, "Leap second handling".
    Serial.print("L ");
    Serial.println(r.gpstime.v);
  }
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
    webcontent.setLocalClock(r.pps, offsetHuman, ClockPID.d(), ClockPID.d_chi(), localClock.getPpb());
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
    webcontent.setHoldover(hs.inHoldover, hs.dispersion, hs.holdoverStartTime);

    msec = 0;
  }
}

static void gps_serial_poll() {
  if(GPS_SERIAL.available()) {
    if(gps.decode()) {
      DateTime now = gps.GPSnow();
      TaiNtpTime gpstime = now.ntptime();
      // Always record what the GPS module itself just reported, regardless
      // of whether the sanity check or ClockDiscipline below ends up
      // trusting it -- a cold-start GPS module reporting *any* date (even
      // an implausible pre-almanac one the check below rejects) is real
      // evidence of progress the served "NTP time" field alone can't show.
      // See TODO.md/DONE.md, "Web UI: show the raw GPS-reported date/time".
      webcontent.setGpsTime(gpstime);
      // secondstime(), not ntptime(): ntptime() is wire-format-constrained
      // and wraps at 2036-02-07, which would eventually make this
      // comparison permanently reject every real GPS fix as "bad". See
      // DateTime::secondstime() and TODO.md/DONE.md, "NTP timestamp era
      // rollover (Y2036)".
      if(now.secondstime() < compileSecondsTime) {
        Serial.print("B "); // gps clock bad message (for example, on startup before GPS almanac)
        Serial.println(gpstime.v);
      } else {
        updateTime(gpstime);
      }
    }
  }
}

// useful when using teensy_loader_cli
static BootloaderMagic bootloaderMagic;
static void bootloader_poll() {
  if(Serial.available()) {
    if(bootloaderMagic.feed(Serial.read())) {
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
