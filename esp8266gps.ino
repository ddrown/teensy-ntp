#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <SoftwareSerial.h>
#include "DateTime.h"
#include "GPS.h"
#include "NTPClock.h"
#include "ClockPID.h"
#include "NTPServer.h"
#include "settings.h"

#define BAUD_SERIAL 115200
#define BAUD_LOGGER 115200
#define RXBUFFERSIZE 1024
#define PPSPIN 5

SoftwareSerial logger(3, 1);
GPSDateTime gps(&Serial);
NTPClock localClock;
WiFiUDP udp;
IPAddress logDestination;
NTPServer server(&udp, &localClock);

uint32_t lastPPS = 0;
uint8_t lastLed = 0;

//ISR for PPS interrupt
void ICACHE_RAM_ATTR handleInterrupt() {
  lastPPS = esp_get_cycle_count();
  lastLed = !lastLed;
  digitalWrite(LED_BUILTIN, lastLed);
}

uint32_t compileTime;
void setup() {
  DateTime compile = DateTime(__DATE__, __TIME__);
  
  Serial.begin(BAUD_SERIAL);
  Serial.setRxBufferSize(RXBUFFERSIZE);
  // Move hardware serial to RX:GPIO13 TX:GPIO15
  Serial.swap();
  // use SoftwareSerial on regular RX(3)/TX(1) for logging
  logger.begin(BAUD_LOGGER);
  logger.enableIntTx(false);
  logger.println("\n\nUsing SoftwareSerial for logging");

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PPSPIN, INPUT);
  digitalWrite(PPSPIN, LOW);
  attachInterrupt(digitalPinToInterrupt(PPSPIN), handleInterrupt, RISING);  
  
  compileTime = compile.ntptime();
  localClock.setTime(esp_get_cycle_count(), compileTime);
  // allow for compile timezone to be 12 hours ahead
  compileTime -= 12*60*60;

  logger.print("Connecting to SSID: ");
  logger.println(ssid);
  WiFi.begin(ssid, ssidPass);
  WiFi.setSleepMode(WIFI_NONE_SLEEP, 0); // no sleeping for minimum latency

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    logger.print(".");
  }
  logger.println();

  logger.print("WiFi connected, IP address: ");
  logger.println(WiFi.localIP());

  logDestination.fromString(logDestinationIP);
  udp.begin(123);
  
  while(Serial.available()) { // throw away all the text received while starting up
    Serial.read();
  }
}

uint8_t settime = 0;
uint8_t wait = 0;
uint32_t lastUpdate = 0;
void loop() {
  server.poll();
  uint32_t cyclesNow = esp_get_cycle_count();
  if((cyclesNow - lastUpdate) >= 80000000) {
    uint32_t s, s_fb;
    // update the local clock's cycle count
    localClock.getTime(cyclesNow,&s,&s_fb);
    lastUpdate = cyclesNow;
  }
  server.poll();
  if(Serial.available()) {
    if(gps.decode()) {
      uint32_t gpstime = gps.GPSnow().ntptime();
      if(gpstime < compileTime) {
        udp.beginPacket(logDestination, 51413);
        udp.print("B ");
        udp.println(gpstime);
        udp.endPacket();
      } else {
        if(lastPPS > 0) {
          if(settime) {
            int64_t offset = localClock.getOffset(lastPPS, gpstime, 0);
            if(ClockPID.full() && wait) {
              wait--;
            } else {
              ClockPID.add_sample(lastPPS, gpstime, offset);
              localClock.setRefTime(gpstime);
              localClock.setPpb(ClockPID.out() * 1000000000.0);
              wait = 2; // (2+1)*16=48s, 80MHz wraps at 53s
            }
            double offsetHuman = offset / (double)4294967296.0;
            udp.beginPacket(logDestination, 51413);
            udp.print(lastPPS);
            udp.print(" ");
            udp.print(offsetHuman, 9);
            udp.print(" ");
            udp.print(ClockPID.d(), 9);
            udp.print(" ");
            udp.print(ClockPID.d_chi(), 9);
            udp.print(" ");
            udp.print(localClock.getPpb());
            udp.print(" ");
            udp.print(gpstime);
            udp.print(" ");
            udp.println(wait);
            udp.endPacket();
          } else {
            localClock.setTime(lastPPS, gpstime);
            ClockPID.add_sample(lastPPS, gpstime, 0);
            settime = 1;
            udp.beginPacket(logDestination, 51413);
            udp.print("S ");
            udp.print(lastPPS);
            udp.print(" ");
            udp.println(gpstime);
            udp.endPacket();
          }
          lastPPS = 0;
        }
      }
    }
  }
  server.poll();
}
