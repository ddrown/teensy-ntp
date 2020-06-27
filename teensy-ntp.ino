#include "lwip_t41.h"
#include "lwip/inet.h"
#include "lwip/dhcp.h"
#include "InputCapture.h"
#include "DateTime.h"
#include "GPS.h"
#include "NTPClock.h"
#include "ClockPID.h"
#include "NTPServer.h"
#include "platform-clock.h"

#define GPS_BAUD 115200
#define GPS_SERIAL Serial1
#define WAIT_COUNT 3

GPSDateTime gps(&GPS_SERIAL);
NTPClock localClock;
InputCapture pps;
elapsedMillis msec;
uint32_t compileTime;
uint8_t settime = 0;
uint8_t wait = WAIT_COUNT-1;
struct {
  int64_t offset;
  uint32_t pps;
  uint32_t gpstime;
} samples[WAIT_COUNT];
NTPServer server(&localClock);

static void netif_status_callback(struct netif *netif) {
  static char str1[IP4ADDR_STRLEN_MAX], str2[IP4ADDR_STRLEN_MAX], str3[IP4ADDR_STRLEN_MAX];
  Serial.printf("netif status changed: ip %s, mask %s, gw %s\n", ip4addr_ntoa_r(netif_ip_addr4(netif), str1, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_netmask4(netif), str2, IP4ADDR_STRLEN_MAX), ip4addr_ntoa_r(netif_ip_gw4(netif), str3, IP4ADDR_STRLEN_MAX));
}

static void link_status_callback(struct netif *netif) {
  Serial.printf("enet link status: %s\n", netif_is_link_up(netif) ? "up" : "down");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10); // wait for usb serial

  DateTime compile = DateTime(__DATE__, __TIME__);

  GPS_SERIAL.begin(GPS_BAUD);

  Serial.println("Ethernet 1588 NTP Server");
  Serial.println("------------------------\n");

  enet_init(NULL, NULL, NULL);

  netif_set_status_callback(netif_default, netif_status_callback);
  netif_set_link_callback(netif_default, link_status_callback);
  netif_set_up(netif_default);
  dhcp_start(netif_default);

  Serial.println("waiting for link");
  while (!netif_is_link_up(netif_default)) {
    enet_proc_input(); // await on link up
    enet_poll();
    delay(1);
  }

  pps.begin();
  server.setup();

  compileTime = compile.ntptime();
  // this needs to happen after enet_init, so the 1588 clock is running
  localClock.setTime(COUNTERFUNC(), compileTime);
  // allow for compile timezone to be 12 hours ahead
  compileTime -= 12*60*60;

  while(GPS_SERIAL.available()) { // throw away all the text received while starting up
    GPS_SERIAL.read();
  }
  msec = 0;
}

static uint8_t median(int64_t one, int64_t two, int64_t three) {
  if(one > two) {
    if(one > three) {
      if(two > three) {
        // 1 2 3
        return 2-1;
      } else {
        // 1 3 2
        return 3-1;
      }
    } else {
      // 3 1 2
      return 1-1;
    }
  } else {
    if(two > three) {
      if(one > three) {
        // 2 1 3
        return 1-1;
      } else {
        // 2 3 1
        return 3-1;
      }
    } else {
      // 3 2 1
      return 2-1;
    }
  }
}

static uint32_t ntp64_to_32(int64_t offset) {
  if(offset < 0)
    offset *= -1;
  // take 16bits off the bottom and top
  offset = offset >> 16;
  return offset & 0xffffffff;
}

void updateTime(uint32_t gpstime) {
  if(pps.getMillis() == 0) {
    return;
  }

  uint32_t ppsToGPS = millis() - pps.getMillis();
  if(ppsToGPS > 950) { // allow 950ms between PPS and GPS message
    Serial.print("LAG ");
    Serial.print(ppsToGPS);
    Serial.print(" ");
    Serial.println(gpstime);
    return;
  }

  uint32_t lastPPS = pps.getCount();
  if(settime) {
    int64_t offset = localClock.getOffset(lastPPS, gpstime, 0);
    samples[wait].offset = offset;
    samples[wait].pps = lastPPS;
    samples[wait].gpstime = gpstime;
    if(ClockPID.full() && wait) {
      wait--;
    } else {
      uint8_t median_index = wait;
      if(wait == 0) {
        median_index = median(samples[0].offset, samples[1].offset, samples[2].offset);
      }
      ClockPID.add_sample(samples[median_index].pps, samples[median_index].gpstime, samples[median_index].offset);
      localClock.setRefTime(samples[median_index].gpstime);
      localClock.setPpb(ClockPID.out() * 1000000000.0);
      wait = WAIT_COUNT-1; // (2+1)*16=48s, 80MHz wraps at 53s

      // TODO: this should grow when out of sync
      server.setDispersion(ntp64_to_32(samples[median_index].offset));
      server.setReftime(samples[median_index].gpstime);

      double offsetHuman = samples[median_index].offset / (double)4294967296.0;
      Serial.print(samples[median_index].pps);
      Serial.print(" ");
      Serial.print(offsetHuman, 9);
      Serial.print(" ");
      Serial.print(ClockPID.d(), 9);
      Serial.print(" ");
      Serial.print(ClockPID.d_chi(), 9);
      Serial.print(" ");
      Serial.print(localClock.getPpb());
      Serial.print(" ");
      Serial.println(samples[median_index].gpstime);
    }
  } else {
    localClock.setTime(lastPPS, gpstime);
    ClockPID.add_sample(lastPPS, gpstime, 0);
    settime = 1;
    Serial.print("S "); // clock set message
    Serial.print(lastPPS);
    Serial.print(" ");
    Serial.println(gpstime);
  }
}

static void slower_poll() {
  if(msec >= 1000) {
    uint32_t s, s_fb;
    // update the local clock's cycle count
    localClock.getTime(COUNTERFUNC(),&s,&s_fb);

    // check link state, update dhcp, etc
    enet_poll();

    msec = 0;
  }
}

static void gps_serial_poll() {
  if(GPS_SERIAL.available()) {
    if(gps.decode()) {
      uint32_t gpstime = gps.GPSnow().ntptime();
      if(gpstime < compileTime) {
        Serial.print("B "); // gps clock bad message (for example, on startup before GPS almanac)
        Serial.println(gpstime);
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
