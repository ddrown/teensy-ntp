#pragma once

#include <stddef.h>
#include <stdint.h>
#include <Arduino.h>
#include "GPS.h"

// long enough to reliably catch at least one full NMEA burst (GPS modules
// typically emit one per second, right after each PPS pulse) even right at
// the edge of a probe window's start.
#ifndef GPS_BAUD_PROBE_MS
#define GPS_BAUD_PROBE_MS 2000
#endif

// Tries each of candidates[0..numCandidates) against serialPort in turn,
// reusing GPS.cpp's own checksum verification (via gps.decode()) rather than
// reimplementing it -- the first candidate that yields a complete,
// checksum-valid ZDA/RMC sentence within GPS_BAUD_PROBE_MS wins. Falls back
// to candidates[0] if none do, so a module that's just slow to start (vs.
// actually misconfigured) still gets a sensible rate to keep listening at.
// See teensy-ntp.ino's setup(), which calls this with GPS_SERIAL/the global
// gps.
//
// Templated on the concrete serial type rather than taking a Stream&: begin()
// isn't part of Stream's interface (only concrete types like HardwareSerial
// or ArduinoFake's mocked Serial_ declare it), and gps must already be bound
// to the same underlying stream (via its constructor), same as production
// wires GPS_SERIAL to the global gps. This also sidesteps GPS_SERIAL (Serial1)
// not even being declared in the host test build -- HardwareSerial.h only
// declares it behind `#if defined(UBRR1H)`, which is never defined on host --
// so tests instantiate this against ArduinoFake's mocked Serial instead.
template<typename SerialT>
uint32_t detectGpsBaud(SerialT &serialPort, GPSDateTime &gps, const uint32_t *candidates, size_t numCandidates) {
  for(size_t i = 0; i < numCandidates; i++) {
    uint32_t candidate = candidates[i];
    serialPort.begin(candidate);
    while(serialPort.available()) { // drop whatever's buffered from the previous rate
      serialPort.read();
    }
    uint32_t probeStart = millis();
    while(millis() - probeStart < GPS_BAUD_PROBE_MS) {
      if(serialPort.available() && gps.decode()) {
        return candidate;
      }
    }
  }

  uint32_t fallback = candidates[0];
  serialPort.begin(fallback);
  return fallback;
}
