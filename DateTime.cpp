// originally from https://raw.githubusercontent.com/DennisSc/PPS-ntp-server/master/src/DateTime.cpp
#include "DateTime.h"

#define SECONDS_PER_DAY 86400L
#define SECONDS_FROM_1900_TO_2000 3155673600
#define SECONDS_FROM_1970_TO_2000 946684800

//has to be const or compiler compaints
const uint8_t daysInMonth [] PROGMEM = {31,28,31,30,31,30,31,31,30,31,30,31};

// number of days since 2000/01/01, valid for 2001..2178
static uint16_t date2days(uint16_t y, uint16_t m, uint16_t d) {
  if (y >= 2000) {
    y -= 2000;
  }
  uint16_t days = d;
  for (uint16_t i = 1; i < m; ++i) {
    days += pgm_read_byte(daysInMonth + i - 1);
  }
  boolean leapyear = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
  if (m > 2 && leapyear) {
    ++days;
  }
  uint16_t leapdays = (y + 3) / 4 - 1;
  if (y > 100) {
    leapdays--; // 2100 is not a leap year
  }
  return days + 365 * y + leapdays;
}

static long time2long(uint16_t days, uint16_t h, uint16_t m, uint16_t s) {
  return ((days * 24L + h) * 60 + m) * 60 + s;
}

////////////////////////////////////////////////////////////////////////////////
// DateTime implementation - ignores time zones and DST changes
// NOTE: also ignores leap seconds, see http://en.wikipedia.org/wiki/Leap_second

// expects NTP timestamp
DateTime::DateTime(uint32_t t) {
  // bring to 2000 timestamp from 1900
  t -= SECONDS_FROM_1900_TO_2000;

  second_ = t % 60;
  t /= 60;
  minute_ = t % 60;
  t /= 60;
  hour_ = t % 24;
  uint16_t days = t / 24;
  uint16_t leap;
  for (year_ = 0; ; ++year_) {
    leap = year_ % 4 == 0;
    if (days < 365 + leap) {
      break;
    }
    days -= 365 + leap;
  }
  for (month_ = 1; ; ++month_) {
    uint16_t daysPerMonth = pgm_read_byte(daysInMonth + month_ - 1);
    if (leap && month_ == 2) {
      ++daysPerMonth;
    }
    if (days < daysPerMonth) {
      break;
    }
    days -= daysPerMonth;
  }
  day_ = days + 1;
}

void DateTime::time(uint32_t t) {
  // bring to 2000 timestamp from 1900
  t -= SECONDS_FROM_1900_TO_2000;

  second_ = t % 60;
  t /= 60;
  minute_ = t % 60;
  t /= 60;
  hour_ = t % 24;
  uint16_t days = t / 24;
  uint16_t leap;
  for (year_ = 0; ; ++year_) {
    leap = year_ % 4 == 0;
    if (days < 365 + leap) {
      break;
    }
    days -= 365 + leap;
  }
  for (month_ = 1; ; ++month_) {
    uint16_t daysPerMonth = pgm_read_byte(daysInMonth + month_ - 1);
    if (leap && month_ == 2) {
      ++daysPerMonth;
    }
    if (days < daysPerMonth) {
      break;
    }
    days -= daysPerMonth;
  }
  day_ = days + 1;
}

DateTime::DateTime(
  uint16_t year, uint16_t month,
  uint16_t day, uint16_t hour,
  uint16_t minute, uint16_t second)
  : year_( (year >= 2000) ? year - 2000 : year),
    month_(month),
    day_(day),
    hour_(hour),
    minute_(minute),
    second_(second) {}

static uint16_t conv2d(const char *p) {
  uint16_t v = 0;
  if ('0' <= *p && *p <= '9') {
    v = *p - '0';
  }
  return 10 * v + *++p - '0';
}

// A convenient constructor for using "the compiler's time":
//   DateTime now (__DATE__, __TIME__);
// NOTE: using PSTR would further reduce the RAM footprint
DateTime::DateTime(
  const char *date,
  const char *time) {
  // sample input: date = "Dec 26 2009", time = "12:34:56"
  year_ = conv2d(date + 9);
  // Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
  switch (date[0]) {
    case 'J':
      month_ = date[1] == 'a' ? 1 : (date[2] == 'n' ? 6 : 7);
      break;
    case 'F':
      month_ = 2;
      break;
    case 'A':
      month_ = date[2] == 'r' ? 4 : 8;
      break;
    case 'M':
      month_ = date[2] == 'r' ? 3 : 5;
      break;
    case 'S':
      month_ = 9;
      break;
    case 'O':
      month_ = 10;
      break;
    case 'N':
      month_ = 11;
      break;
    case 'D':
      month_ = 12;
      break;
    default:
      break;
  }
  day_ = conv2d(date + 4);
  hour_ = conv2d(time);
  minute_ = conv2d(time + 3);
  second_ = conv2d(time + 6);
}

uint32_t DateTime::ntptime(void) const {
  uint32_t t;
  uint16_t days = date2days(year_, month_, day_);
  t = time2long(days, hour_, minute_, second_);
  t += SECONDS_FROM_1900_TO_2000;

  return t;
}

uint32_t DateTime::unixtime(void) const {
  uint32_t t;
  uint16_t days = date2days(year_, month_, day_);
  t = time2long(days, hour_, minute_, second_);
  t += SECONDS_FROM_1970_TO_2000;

  return t;
}

String DateTime::toString(void) {
  sprintf(chartime, "%02d:%02d:%02d", hour(), minute(), second() );
  return chartime;
}

void DateTime::print(Stream *out) const {
  out->print(year());
  out->print("-");
  out->print(month());
  out->print("-");
  out->print(day());
  out->print(" ");
  out->print(hour());
  out->print(":");
  out->print(minute());
  out->print(":");
  out->print(second());
}
