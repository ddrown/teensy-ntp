#pragma once

#include "NtpTimestamp.h"

class WebContent {
  public:
    void begin();
    const char *jsonState();
    void setPPSData(uint32_t new_ppsToGPS, uint32_t new_ppsMillis);
    void setLocalClock(uint32_t new_counterPPS, double new_offsetHuman, double new_pidD, double new_dChiSq, int32_t new_clockPpb);
    // new_holdoverStartTime is only meaningful when new_inHoldover is true --
    // a fixed UTC timestamp (the last accepted sample's own time, plus the
    // staleness grace window) rather than a live-ticking duration, so it
    // doesn't go stale between this being pushed and a client polling
    // state.json up to ~1s later (slower_poll() only calls this ~1/sec).
    void setHoldover(bool new_inHoldover, uint32_t new_holdoverDispersion, TaiNtpTime new_holdoverStartTime);
    // Raw date/time the GPS module's own NMEA sentences most recently
    // reported, independent of whether ClockDiscipline accepted it (or even
    // whether gps_serial_poll()'s own compileSecondsTime sanity check did) --
    // a valid GPS-reported date is the second sign of life the module gives
    // after a cold start (after satellite counts start climbing, before PPS/
    // lock are fully established), which the served "NTP time" field alone
    // doesn't show.
    void setGpsTime(TaiNtpTime new_gpstime);

  private:
    char jsonBuffer[1500] = "";
    uint32_t ppsToGPS = 0, ppsMillis = 0;
    uint32_t counterPPS = 0;
    double offsetHuman = 0, pidD = 0, dChiSq = 0;
    int32_t clockPpb = 0;
    bool inHoldover = false;
    uint32_t holdoverDispersion = 0;
    uint32_t holdoverStartTime = 0; // wire format; only meaningful when inHoldover is true
    uint32_t gpsReportedTime = 0;
    bool haveGpsReportedTime = false;
};

extern WebContent webcontent;
