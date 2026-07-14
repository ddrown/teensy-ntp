// from https://raw.githubusercontent.com/DennisSc/PPS-ntp-server/master/src/GPS.cpp

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "DateTime.h"
#include "GPS.h"
#include "InputCapture.h"
#include "settings.h"

#define GPS_CODE_ZDA "ZDA"
#define GPS_CODE_RMC "RMC"
#define GPS_CODE_GGA "GGA"
#define GPS_CODE_GSA "GSA"
#define GPS_CODE_GSV "GSV"
 
/**
 * Save new date and time to private variables
 */
void GPSDateTime::commit() {
  time_ = newTime_;
  year_ = newYear_;
  month_ = newMonth_;
  day_ = newDay_;
  // RMC takes the PPS snapshot on the GPS_CODE_GGA message if it is first
#if !defined(GPS_USES_RMC) || !defined(GPS_GGA_IS_FIRST)
  ppsCounter_ = pps.getCount();
  ppsMillis_ = pps.getMillis();
  dateMillis = millis();
#endif
  if(sawGSV) { // sometimes GSV doesn't come every second
    strongSignal = strongSignalNext;
    weakSignal = weakSignalNext;
    noSignal = noSignalNext;
    sawGSV = false;
    satellites_copy = (satellites_copy + 1) % 2;
    satellites[satellites_copy][satellites_i].id = 0;
    satellites_i = 0;
  }
  strongSignalNext = weakSignalNext = noSignalNext = 0;
}

void GPSDateTime::time(const char *time) {
  newTime_ = atof(time) * 100;
}

uint16_t GPSDateTime::hour() {
  return time_ / 1000000;
}

uint16_t GPSDateTime::minute() {
  return (time_ / 10000) % 100;
}

uint16_t GPSDateTime::second() {
  return (time_ / 100) % 100;
}

void GPSDateTime::day(const char *day) {
  newDay_ = atoi(day);
}
uint16_t GPSDateTime::day(void) { return day_; };

void GPSDateTime::month(const char *month) {
  newMonth_ = atoi(month);
}
uint16_t GPSDateTime::month(void) { return month_; };

void GPSDateTime::year(const char *year) {
  newYear_ = atoi(year);
}
uint16_t GPSDateTime::year(void) { return year_; };

void GPSDateTime::rmctime(const char *timestr) {
  newTime_ = atof(timestr) * 100;
}

void GPSDateTime::rmcdate(const char *datestr) {
  int date = atoi(datestr);
  newDay_ = date / 10000;
  newMonth_ = (date / 100) % 100;
  newYear_ = date % 100 + 2000;
}

bool GPSDateTime::tmp_is_code(const char *code) {
  if(tmpLen != 5) {
    return false;
  }
  if(tmp[0] != 'G') {
    return false;
  }
  if(tmp[1] != 'P' && tmp[1] != 'N' && tmp[1] != 'L') {
    return false;
  }
  return strcmp(tmp + 2, code) == 0;
}

// bounds-checked append; silently truncates instead of overflowing --
// real NMEA fields never come close to GPS_FIELD_MAX_LEN
void GPSDateTime::tmp_append(char c) {
  if (tmpLen < sizeof(tmp) - 1) {
    tmp[tmpLen++] = c;
    tmp[tmpLen] = '\0';
  }
}

void GPSDateTime::decodeType() {
#ifdef GPS_USES_RMC
  if (tmp_is_code(GPS_CODE_RMC)) {
    validCode = inTimeCode;
  } else if (tmp_is_code(GPS_CODE_GGA)) {
#ifdef GPS_GGA_IS_FIRST
    ppsCounter_ = pps.getCount();
    ppsMillis_ = pps.getMillis();
    dateMillis = millis();
#endif
    validCode = waitDollar;
#else // GPS_USES_RMC
  if (tmp_is_code(GPS_CODE_ZDA)) {
    validCode = inTimeCode;
#endif
  } else if (tmp_is_code(GPS_CODE_GSA)) {
    validCode = inGSA;
  } else if (tmp_is_code(GPS_CODE_GSV)) {
    sawGSV = true;
    validCode = inGSV;
  } else {
    validCode = waitDollar;
  }
}

void GPSDateTime::decodeTimeCode() {
#ifdef GPS_USES_RMC
  // example $GPRMC,144326.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,210307,0.0,E,A*20
  switch (count_) {
    case 1: // time
      this->rmctime(tmp);
      break;
    case 9:
      this->rmcdate(tmp);
      break;
    default:
      break;
  }
#else
  // example $GPZDA,174304.36,24,11,2015,00,00*66
  switch (count_) {
    case 1: // time
      this->time(tmp);
      break;
    case 2: // day
      this->day(tmp);
      break;
    case 3: // month
      this->month(tmp);
      break;
    case 4: // year
      this->year(tmp);
      break;
    default:
      break;
  }
#endif
}

void GPSDateTime::decodeGSA() {
  // example $GPGSA,A,3,04,07,09,03,08,22,16,27,,,,,1.4,0.8,1.2*3F
  switch(count_) {
    case 2:
      lockStatus_ = atoi(tmp);
      break;
    case 15:
      pdop = atof(tmp);
      break;
    case 16:
      hdop = atof(tmp);
      break;
    case 17:
      vdop = atof(tmp);
      break;
  }
}

void GPSDateTime::decodeGSV() {
  uint8_t writeCopy = (satellites_copy + 1) % 2;
  if(count_ > 3 && count_ < 20) {
    switch(count_ % 4) {
      case 0: // id
        satellites[writeCopy][satellites_i].id = atoi(tmp);
        break;
      case 1: // elevation from horizon in degrees
        satellites[writeCopy][satellites_i].elevation = atoi(tmp);
        break;
      case 2: // azimuth from clockwise north in degrees
        satellites[writeCopy][satellites_i].azimuth = atoi(tmp);
        break;
      case 3: // snr
        satellites[writeCopy][satellites_i].snr = atoi(tmp);
        if(satellites[writeCopy][satellites_i].snr >= 25) {
          strongSignalNext++;
        } else if(satellites[writeCopy][satellites_i].snr >= 10) {
          weakSignalNext++;
        } else {
          noSignalNext++;
        }

        if(satellites_i < MAX_SATELLITES-1) satellites_i++;
        break;
    }
  }
}

/**
 * Decode NMEA lines
 * @return true: finished decoding date&time
 */
bool GPSDateTime::decode() {
  char c = gpsUart_->read();

  if (c == '$') {
    tmp[0] = '\0';
    tmpLen = 0;
    count_ = 0;
    parity_ = 0;
    validCode = getType;
    isNotChecked = true;
    isUpdated_ = false;
    return false;
  }

  if (validCode == waitDollar) {
    return false;
  }
  if (c == ',' || c == '*') {
    switch(validCode) {
      case getType:
        decodeType();
        break;
      case inTimeCode:
        decodeTimeCode();
        break;
      case inGSA:
        decodeGSA();
        break;
      case inGSV:
        decodeGSV();
        break;
      case waitDollar:
        break;
    }
    if (c == ',') {
      parity_ ^= (uint8_t) c;
    }
    if (c == '*') {
      isNotChecked = false;
    }
    tmp[0] = '\0';
    tmpLen = 0;
    count_++;
  } else if (c == '\r' || c == '\n') {
    // carriage return, so check for valid parity
    uint8_t checksum = strtoul( tmp, NULL, 16 );
    validString = parity_ == checksum;

    if (validString) {
      if(validCode == inTimeCode) {
        this->commit();
        isUpdated_ = true;
      }
      // commit datetime
    }

    // end of string
    tmp[0] = '\0';
    tmpLen = 0;
    count_ = 0;
    parity_ = 0;
    validCode = waitDollar;
    return isUpdated_;
  } else {
    // ordinary char
    tmp_append(c);
    if (isNotChecked) {
      // XOR of all characters from $ to *
      parity_ ^= (uint8_t) c;
    }
  }

  return false;
}



/**
 * Return instance of DateTime class
 * @return DateTime
 */
DateTime GPSDateTime::GPSnow() {
  return DateTime(this->year(), this->month(), this->day(), this->hour(), this->minute(), this->second());
}
