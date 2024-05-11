#pragma once

#define MAX_SATELLITES 30

struct satellite {
  uint16_t id;
  uint16_t azimuth;
  uint8_t elevation;
  uint8_t snr;
};

class GPSDateTime {
 public:
  GPSDateTime(Stream *gpsUart): tmp(""), gpsUart_(gpsUart), validCode(waitDollar) {
    lockStatus_ = dateMillis = 0;
    strongSignal = weakSignal = strongSignalNext = weakSignalNext = noSignal = noSignalNext = 0;
    satellites[0][0].id = satellites[0][0].azimuth = satellites[0][0].elevation = satellites[0][0].snr = 0;
    satellites_i = 0;
    satellites_copy = 0;
    pdop = hdop = vdop = 0;
    sawGSV = false;
  };
  void commit(void);
  void time(String time);
  void rmctime(String timestr);
  void rmcdate(String datestr);
  uint16_t hour();
  uint16_t minute();
  uint16_t second();
  void day(String day);
  uint16_t day(void);
  void month(String month);
  uint16_t month(void);
  void year(String year);
  uint16_t year(void);
  DateTime GPSnow();
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

  String tmp;
  Stream *gpsUart_;

  uint8_t count_;
  uint8_t parity_;

  bool isNotChecked;
  enum {waitDollar, getType, inTimeCode, inGSA, inGSV} validCode;
  bool validString;
  bool isUpdated_;
  bool getFlag_;
  bool sawGSV;
  uint32_t dateMillis, ppsCounter_, ppsMillis_;
  uint32_t strongSignal, weakSignal, noSignal, strongSignalNext, weakSignalNext, noSignalNext;
  uint8_t lockStatus_;
  uint8_t satellites_i;
  uint8_t satellites_copy;
  struct satellite satellites[2][MAX_SATELLITES];
  float pdop, hdop, vdop;

  bool debug_;
  String msg;
  DateTime now(void);
};

extern GPSDateTime gps;
