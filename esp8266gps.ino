#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include "DateTime.h"
#include "GPS.h"
#include "NTPClock.h"

#define BAUD_SERIAL 115200
#define BAUD_LOGGER 115200
#define RXBUFFERSIZE 1024
#define PPSPIN 5

SoftwareSerial logger(3, 1);
GPSDateTime gps(&Serial);
NTPClock localClock;

long lastPPS = 0;

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
  localClock.setPpb(-668); // TODO: linear estimation
  // allow for compile timezone to be 12 hours ahead
  compileTime -= 12*60*60;
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
          logger.println(lastPPS);
          if(settime) {
            uint32_t sec, fractional;
            localClock.getTime(lastPPS, &sec, &fractional);
            logger.print(sec);
            logger.print(".");
            logger.println(fractional);
          } else {
            localClock.setTime(lastPPS, gps.GPSnow().ntptime());
            settime = 1;
          }
          lastPPS = 0;
        }
        logger.println(gps.GPSnow().ntptime());
        logger.println("");
      }
    }
  }
}
