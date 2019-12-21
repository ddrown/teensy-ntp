#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <SoftwareSerial.h>
#include "DateTime.h"
#include "GPS.h"
#include "NTPClock.h"
#include "ClockPID.h"
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

uint32_t lastPPS = 0;

//ISR for PPS interrupt
void ICACHE_RAM_ATTR handleInterrupt() {
  lastPPS = micros();
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

  pinMode(PPSPIN, INPUT);
  digitalWrite(PPSPIN, LOW);
  attachInterrupt(digitalPinToInterrupt(PPSPIN), handleInterrupt, RISING);  
  
  compileTime = compile.ntptime();
  localClock.setTime(micros(), compileTime);
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
void loop() {
  if(Serial.available()) {
    if(gps.decode()) {
      if(gps.GPSnow().ntptime() < compileTime) {
        gps.GPSnow().print(&logger);
        logger.print(" < ");
        logger.print(compileTime);
        logger.println("");
      } else {
        if(lastPPS > 0) {
          if(settime) {
            int64_t offset = localClock.getOffset(lastPPS, gps.GPSnow().ntptime(), 0);
            ClockPID.add_sample(lastPPS, gps.GPSnow().ntptime(), offset);
            localClock.setPpb(ClockPID.out() * 1000000000.0);
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
            udp.println(localClock.getPpb());
            udp.endPacket();
          } else {
            localClock.setTime(lastPPS, gps.GPSnow().ntptime());
            ClockPID.add_sample(lastPPS, gps.GPSnow().ntptime(), 0);
            settime = 1;
          }
          lastPPS = 0;
        }
      }
    }
  }
}
