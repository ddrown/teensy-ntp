#pragma once

class GPSDateTime {
 public:
  GPSDateTime(Stream *gpsUart): tmp(""), gpsUart_(gpsUart), validCode(false), dateMillis(0) {  };
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

 protected:
  uint32_t newTime_;
  uint16_t newYear_, newMonth_, newDay_;

  uint32_t time_;
  uint16_t year_, month_, day_;

  DateTime getZDA();
  
 private:
  String tmp;
  Stream *gpsUart_;

  uint8_t count_;
  uint8_t parity_;

  bool isNotChecked;
  bool validCode;
  bool validString;
  bool isUpdated_;
  bool getFlag_;
  uint32_t dateMillis;

  bool debug_;
  String msg;
  DateTime now(void);
};
