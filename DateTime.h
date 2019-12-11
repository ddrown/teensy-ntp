// from https://raw.githubusercontent.com/DennisSc/PPS-ntp-server/master/src/DateTime.h

#ifndef DATETIME_H_
#define DATETIME_H_

#include "Arduino.h"

class DateTime {
 public:
  DateTime(uint32_t t = 0, unsigned long microsfraction = 0);
  DateTime(uint16_t year, uint16_t month, uint16_t day,
    uint16_t hour = 0, uint16_t minute = 0, uint16_t second = 0,
    unsigned long microsfraction = 0);
  DateTime(const char *date, const char *time, unsigned long microsfraction = 0);

  void time(uint32_t t);
  void microsfraction(unsigned long microsfraction) {
    microsfraction_ = microsfraction;
  };

  uint16_t year() const { return 2000 + year_; }
  uint16_t month() const { return month_; }
  uint16_t day() const { return day_; }
  uint16_t hour() const { return hour_; }
  uint16_t minute() const { return minute_; }
  uint16_t second() const { return second_; }
  unsigned long microsfraction() const { return microsfraction_; }
  //uint16_t dayOfWeek(); //const;// { return dayOfWeek_;}

  // 32-bit times as seconds since 1/1/2000
  long secondstime() const;
  // 32-bit times as seconds since 1/1/1900
  uint32_t ntptime(void) const;
  // 32-bit times as seconds since 1/1/1970
  uint32_t unixtime(void) const;

  String toString(void);
  void print(Stream *out) const;
  
 protected:
  uint16_t year_, month_, day_, hour_, minute_, second_;
  unsigned long microsfraction_;

 private:
   char chartime[9]; // for toString
};

#endif  // DATETIME_H_
