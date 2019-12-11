#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
#include "DateTime.h"
#include "GPS.h"

#define BAUD_SERIAL 115200
#define BAUD_LOGGER 115200
#define RXBUFFERSIZE 1024
#define PPSPIN 5

SoftwareSerial logger(3, 1);
GPSDateTime gps(&Serial);

long lastPPS = 0;
long startOffset = 0;

//ISR for PPS interrupt
void ICACHE_RAM_ATTR handleInterrupt() {
  lastPPS = micros();
  if (startOffset == 0) {
    startOffset = lastPPS;
  }
}

void setup() {
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
}

void loop() {
  if(Serial.available()) {
    if(gps.decode()) {
      long now = micros();
      logger.print(now-lastPPS);
      logger.print(" ");
      logger.println(lastPPS-startOffset);
      gps.GPSnow().print(&logger);
    }
  }
}
