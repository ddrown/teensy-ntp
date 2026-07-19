// from https://raw.githubusercontent.com/DennisSc/PPS-ntp-server/master/src/DateTime.h

#pragma once

#include "Arduino.h"
#include "NtpTimestamp.h"

class DateTime {
 public:
  // t is TAI-like (see ntptime() below), not raw NTP wire format.
  explicit DateTime(TaiNtpTime t = TaiNtpTime(0));
  DateTime(uint16_t year, uint16_t month, uint16_t day,
    uint16_t hour = 0, uint16_t minute = 0, uint16_t second = 0);
  DateTime(const char *date, const char *time);

  void time(TaiNtpTime t);

  uint16_t year() const { return 2000 + year_; }
  uint16_t month() const { return month_; }
  uint16_t day() const { return day_; }
  uint16_t hour() const { return hour_; }
  uint16_t minute() const { return minute_; }
  uint16_t second() const { return second_; }

  // 32-bit times as seconds since 1/1/2000, unsigned so it's monotonic with
  // real time all the way out to ~2136 (2^32 seconds from the 2000 epoch) --
  // no NTP-wire-format constraint forcing it to alias onto 1900-2000 the way
  // ntptime() deliberately does, so plain `<` works for comparing two
  // firmware-internal timestamps without a wraparound-aware helper. See
  // TODO.md/DONE.md, "NTP timestamp era rollover (Y2036)".
  uint32_t secondstime() const;
  // 32-bit times as seconds since 1/1/1900, TAI-like: real, leap-second-
  // adjusted seconds, monotonic across a leap second insertion/deletion --
  // NOT the raw NTP wire format. Convert via LeapSeconds.h's
  // taiToWireNtp()/leapSecondOffsetAtTai() before putting this on the wire.
  TaiNtpTime ntptime(void) const;
  // 32-bit times as seconds since 1/1/1970, same TAI-like domain as
  // ntptime() above (leap offset applied relative to the NTP/1900 epoch,
  // per LeapSeconds.h, then rebased to 1970).
  uint32_t unixtime(void) const;

  String toString(void);
  void print(Stream *out) const;
  
 protected:
  uint16_t year_, month_, day_, hour_, minute_, second_;

 private:
   char chartime[9]; // for toString
};
