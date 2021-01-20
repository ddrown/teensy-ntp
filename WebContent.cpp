#include <Arduino.h>
#include "WebServer.h"
#include "WebContent.h"

WebContent webcontent;

static const char *jsonState(const char *filename) {
  return webcontent.jsonState();
}

static const struct webpage webpages[] = {
  {
    "See /state.json for current state",
    "/index.html",
    NULL
  },
  {
    "File not found",
    "/404.html",
    NULL
  },
  {
    NULL,
    "/state.json",
    jsonState
  },
  {
    NULL, NULL, NULL
  }
};

void WebContent::begin() {
  webserver.setWebpages(webpages);
}

const char *WebContent::jsonState() {
  snprintf(
    jsonBuffer,
    sizeof(jsonBuffer), 
    "{\"ppsToGPS\": %lu, \"ppsMillis\": %lu, \"curMillis\": %lu, \"gpstime\": %lu, \"counterPPS\": %lu, \"offsetHuman\": %.9f, \"pidD\": %.9f, \"dChiSq\": %.9f, \"clockPpb\": %ld}",
    ppsToGPS,
    ppsMillis,
    millis(),
    gpstime,
    counterPPS,
    offsetHuman,
    pidD,
    dChiSq,
    clockPpb
  );
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  return jsonBuffer;
}

void WebContent::setPPSData(uint32_t new_ppsToGPS, uint32_t new_ppsMillis, uint32_t new_gpstime) {
  ppsToGPS = new_ppsToGPS;
  ppsMillis = new_ppsMillis;
  gpstime = new_gpstime;
}

void WebContent::setLocalClock(uint32_t new_counterPPS, double new_offsetHuman, double new_pidD, double new_dChiSq, int32_t new_clockPpb, uint32_t new_gpstime) {
  counterPPS = new_counterPPS;
  offsetHuman = isnan(new_offsetHuman) ? 0 : new_offsetHuman;
  pidD = isnan(new_pidD) ? 0 : new_pidD;
  dChiSq = isnan(new_dChiSq) ? 0 : new_dChiSq;
  clockPpb = new_clockPpb;
  gpstime = new_gpstime;
}
