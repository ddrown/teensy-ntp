#include <Arduino.h>
#include "WebServer.h"
#include "WebContent.h"
#include "DateTime.h"
#include "GPS.h"
#include "LeapSeconds.h"
#include "NTPClock.h"

#include "index_html.h"
#include "index_js.h"

WebContent webcontent;

static const char *jsonState(const char *filename) {
  return webcontent.jsonState();
}

static const struct webpage webpages[] = {
  {
    indexHTML,
    "/index.html",
    NULL
  },
  {
    indexJS,
    "/index.js",
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
  // Always the live disciplined clock, not the last real GPS-derived sample --
  // the displayed field is labeled "NTP time" (index_html.h), i.e. what this
  // server is currently serving, not "when did GPS last report." Reading
  // localClock directly here also means there's nothing to freeze: no
  // GPS-fix-received flag to latch, no holdover special case, no window
  // before the first fix where there's nothing to show yet -- localClock
  // itself is always running (compile-time-seeded until the first real
  // setTime()). See DONE.md, "WebContent gpstime freeze during holdover".
  uint32_t displayGpstime = 0;
  TaiNtpTime now;
  uint32_t nowFractional;
  if(localClock.getTime(&now, &nowFractional)) {
    displayGpstime = taiToWireNtp(now).v;
  }

  int total = sizeof(jsonBuffer);
  int offset = snprintf(
    jsonBuffer,
    sizeof(jsonBuffer),
    "{\"ppsToGPS\": %lu, \"ppsMillis\": %lu, \"curMillis\": %lu, \"gpstime\": %lu, \"counterPPS\": %lu, \"offsetHuman\": %.9f, \"pidD\": %.9f, \"dChiSq\": %.9f, \"clockPpb\": %ld, \"inHoldover\": %d, \"holdoverDispersion\": %lu, \"holdoverStartTime\": %lu, \"gpsReportedTime\": %lu, \"haveGpsReportedTime\": %d,",
    ppsToGPS,
    ppsMillis,
    millis(),
    displayGpstime,
    counterPPS,
    offsetHuman,
    pidD,
    dChiSq,
    clockPpb,
    inHoldover ? 1 : 0,
    holdoverDispersion,
    holdoverStartTime,
    gpsReportedTime,
    haveGpsReportedTime ? 1 : 0
  );
  if (offset >= total) {
    jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
    return jsonBuffer;
  }
  offset += snprintf(jsonBuffer + offset, sizeof(jsonBuffer) - offset,
      "\"lockStatus\": %u, \"strongSignals\": %lu, \"weakSignals\": %lu, \"noSignals\": %lu, \"gpsCaptured\": %lu, \"pdop\": %.1f, \"hdop\": %.1f, \"vdop\": %.1f, \"satellites\": [",
      gps.lockStatus(),
      gps.strongSignals(),
      gps.weakSignals(),
      gps.noSignals(),
      gps.capturedAt(),
      gps.getPdop(),
      gps.getHdop(),
      gps.getVdop()
      );
  if (offset >= total) {
    jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
    return jsonBuffer;
  }

  const struct satellite *satinfo = gps.getSatellites();
  for(uint8_t i = 0; i < MAX_SATELLITES && satinfo[i].id; i++) {
    const char *format = (i == 0) ? "[%u,%u,%u,%u]" : ",[%u,%u,%u,%u]";
    offset += snprintf(jsonBuffer + offset, sizeof(jsonBuffer) - offset,
        format, satinfo[i].id, satinfo[i].elevation, satinfo[i].azimuth, satinfo[i].snr
        );
    if (offset >= total) {
      jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
      return jsonBuffer;
    }
  }
  snprintf(jsonBuffer + offset, sizeof(jsonBuffer) - offset, "]}");
  jsonBuffer[sizeof(jsonBuffer)-1] = '\0';
  return jsonBuffer;
}

void WebContent::setPPSData(uint32_t new_ppsToGPS, uint32_t new_ppsMillis) {
  ppsToGPS = new_ppsToGPS;
  ppsMillis = new_ppsMillis;
}

void WebContent::setLocalClock(uint32_t new_counterPPS, double new_offsetHuman, double new_pidD, double new_dChiSq, int32_t new_clockPpb) {
  counterPPS = new_counterPPS;
  offsetHuman = isnan(new_offsetHuman) ? 0 : new_offsetHuman;
  pidD = isnan(new_pidD) ? 0 : new_pidD;
  dChiSq = isnan(new_dChiSq) ? 0 : new_dChiSq;
  clockPpb = new_clockPpb;
}

void WebContent::setHoldover(bool new_inHoldover, uint32_t new_holdoverDispersion, TaiNtpTime new_holdoverStartTime) {
  inHoldover = new_inHoldover;
  holdoverDispersion = new_holdoverDispersion;
  holdoverStartTime = taiToWireNtp(new_holdoverStartTime).v;
}

void WebContent::setGpsTime(TaiNtpTime new_gpstime) {
  gpsReportedTime = taiToWireNtp(new_gpstime).v;
  haveGpsReportedTime = true;
}
