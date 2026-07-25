#pragma once

#define MAX_SATELLITES 30
// generous bound on a single NMEA field's length (a full sentence is
// bounded to 82 chars per spec; individual fields are much shorter, e.g.
// the longest lat/lon fields are ~13 chars)
#define GPS_FIELD_MAX_LEN 32

struct satellite {
  uint16_t id;
  uint16_t azimuth;
  uint8_t elevation;
  uint8_t snr;
};

class GPSDateTime {
 public:
  GPSDateTime(Stream *gpsUart): gpsUart_(gpsUart), validCode(waitDollar) {
    tmp[0] = '\0';
    tmpLen = 0;
    lockStatus_ = dateMillis = 0;
    everCapturedPps_ = false;
    committedThisPulse_ = false;
    reportUpdated_ = false;
    capturedPpsCaptures_ = 0;
    strongSignal = weakSignal = strongSignalNext = weakSignalNext = noSignal = noSignalNext = 0;
    satellites[0][0].id = satellites[0][0].azimuth = satellites[0][0].elevation = satellites[0][0].snr = 0;
    satellites_i = 0;
    satellites_copy = 0;
    pdop = hdop = vdop = 0;
    sawGSV = false;
  };
  void commit(void);
  void time(const char *time);
  void rmctime(const char *timestr);
  void rmcdate(const char *datestr);
  uint16_t hour();
  uint16_t minute();
  uint16_t second();
  void day(const char *day);
  uint16_t day(void);
  void month(const char *month);
  uint16_t month(void);
  void year(const char *year);
  uint16_t year(void);
  DateTime GPSnow();
  DateTime reportedNow();
  bool reportedUpdate() { bool result = reportUpdated_; reportUpdated_ = false; return result; };
  bool decode();
  uint32_t capturedAt() { return dateMillis; };
  uint32_t ppsCounter() { return ppsCounter_; };
  uint32_t ppsMillis() { return ppsMillis_; };
  uint8_t lockStatus() { return lockStatus_; };
  uint32_t strongSignals() { return strongSignal; };
  uint32_t weakSignals() { return weakSignal; };
  uint32_t noSignals() { return noSignal; };
  const struct satellite *getSatellites() { return satellites[satellites_copy]; };
  float getPdop() { return pdop; };
  float getHdop() { return hdop; };
  float getVdop() { return vdop; };

 protected:
  uint32_t newTime_;
  uint16_t newYear_, newMonth_, newDay_;

  uint32_t time_;
  uint16_t year_, month_, day_;

  DateTime getZDA();
  
 private:
  void decodeType();
  void decodeTimeCode();
  void decodeGSA();
  void decodeGSV();
  bool tmp_is_code(const char *code);
  void tmp_append(char c);

  char tmp[GPS_FIELD_MAX_LEN];
  uint8_t tmpLen;
  Stream *gpsUart_;

  uint8_t count_;
  uint8_t parity_;

  bool isNotChecked;
  enum {waitDollar, getType, inZDATimeCode, inRMCTimeCode, inGSA, inGSV} validCode;
  bool validString;
  bool isUpdated_;
  bool reportUpdated_;
  bool getFlag_;
  bool sawGSV;
  bool everCapturedPps_;
  bool committedThisPulse_;
  uint32_t capturedPpsCaptures_;
  uint32_t dateMillis, ppsCounter_, ppsMillis_;
  uint32_t strongSignal, weakSignal, noSignal, strongSignalNext, weakSignalNext, noSignalNext;
  uint8_t lockStatus_;
  uint8_t satellites_i;
  uint8_t satellites_copy;
  struct satellite satellites[2][MAX_SATELLITES];
  float pdop, hdop, vdop;

  bool debug_;
  DateTime now(void);
};

extern GPSDateTime gps;
