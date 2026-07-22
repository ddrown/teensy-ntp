#pragma once

#include "NtpTimestamp.h"

class WebContent {
  public:
    void begin();
    const char *jsonState();
    void setPPSData(uint32_t new_ppsToGPS, uint32_t new_ppsMillis, TaiNtpTime new_gpstime);
    void setLocalClock(uint32_t new_counterPPS, double new_offsetHuman, double new_pidD, double new_dChiSq, int32_t new_clockPpb, TaiNtpTime new_gpstime);
    void setHoldover(bool new_inHoldover, uint32_t new_holdoverDispersion, uint32_t new_holdoverElapsedMs);

  private:
    char jsonBuffer[1500] = "";
    uint32_t ppsToGPS = 0, ppsMillis = 0, gpstime = 0;
    // until the first real GPS-derived timestamp arrives, jsonState() reports
    // localClock's free-running (compile-time-seeded) time instead of the
    // zero-initialized gpstime above, so the web UI's graphs don't sit stuck
    // at the NTP epoch (1900) across a power cycle -- see setPPSData().
    bool haveGpsTime = false;
    uint32_t counterPPS = 0;
    double offsetHuman = 0, pidD = 0, dChiSq = 0;
    int32_t clockPpb = 0;
    bool inHoldover = false;
    uint32_t holdoverDispersion = 0, holdoverElapsedMs = 0;
};

extern WebContent webcontent;
