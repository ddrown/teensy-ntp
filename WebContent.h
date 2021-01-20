#pragma once

class WebContent {
  public:
    void begin();
    const char *jsonState();
    void setPPSData(uint32_t new_ppsToGPS, uint32_t new_ppsMillis, uint32_t new_gpstime);
    void setLocalClock(uint32_t new_counterPPS, double new_offsetHuman, double new_pidD, double new_dChiSq, int32_t new_clockPpb, uint32_t new_gpstime);

  private:
    char jsonBuffer[1500] = "";
    uint32_t ppsToGPS = 0, ppsMillis = 0, gpstime = 0;
    uint32_t counterPPS = 0;
    double offsetHuman = 0, pidD = 0, dChiSq = 0;
    int32_t clockPpb = 0;
};

extern WebContent webcontent;
