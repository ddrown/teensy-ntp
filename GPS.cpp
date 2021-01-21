// from https://raw.githubusercontent.com/DennisSc/PPS-ntp-server/master/src/GPS.cpp

#include <Arduino.h>
#include "DateTime.h"
#include "GPS.h"
#include "InputCapture.h"
#include "settings.h"

#define GPS_CODE_ZDA "GPZDA"
#define GPS_CODE2_ZDA "GNZDA"
#define GPS_CODE_RMC "GPRMC"
#define GPS_CODE_GGA "GPGGA"
 
/**
 * Save new date and time to private variables
 */
void GPSDateTime::commit() {
  time_ = newTime_;
  year_ = newYear_;
  month_ = newMonth_;
  day_ = newDay_;
  // RMC takes the PPS snapshot on the GPS_CODE_GGA message
#ifndef GPS_USES_RMC
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

void GPSDateTime::time(String time) {
  newTime_ = time.toFloat() * 100;
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

void GPSDateTime::day(String day) {
  newDay_ = day.toInt();
}
uint16_t GPSDateTime::day(void) { return day_; };

void GPSDateTime::month(String month) {
  newMonth_ = month.toInt();
}
uint16_t GPSDateTime::month(void) { return month_; };

void GPSDateTime::year(String year) {
  newYear_ = year.toInt();
}
uint16_t GPSDateTime::year(void) { return year_; };

void GPSDateTime::rmctime(String timestr) {
  newTime_ = timestr.toFloat() * 100;
}

void GPSDateTime::rmcdate(String datestr) {
  int date = datestr.toInt();
  newDay_ = date / 10000;
  newMonth_ = (date / 100) % 100;
  newYear_ = date % 100 + 2000;
}

void GPSDateTime::decodeType() {
#ifdef GPS_USES_RMC
  if (tmp.equals(GPS_CODE_RMC)) {
    validCode = inTimeCode;
  } else if (tmp.equals(GPS_CODE_GGA)) {
    ppsCounter_ = pps.getCount();
    ppsMillis_ = pps.getMillis();
    dateMillis = millis();
    validCode = waitDollar;
#else // GPS_USES_RMC
  if (tmp.equals(GPS_CODE_ZDA) || tmp.equals(GPS_CODE2_ZDA)) {
    validCode = inTimeCode;
#endif
  } else if (tmp.length() == 5 && tmp[2] == 'G' && tmp[3] == 'S' && tmp[4] == 'A') {
    validCode = inGSA;
  } else if (tmp.length() == 5 && tmp[2] == 'G' && tmp[3] == 'S' && tmp[4] == 'V') {
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
      lockStatus_ = tmp.toInt();
      break;
    case 15:
      pdop = tmp.toFloat();
      break;
    case 16:
      hdop = tmp.toFloat();
      break;
    case 17:
      vdop = tmp.toFloat();
      break;
  }
}

void GPSDateTime::decodeGSV() {
  uint8_t writeCopy = (satellites_copy + 1) % 2;
  if(count_ > 3 && count_ < 20) {
    switch(count_ % 4) {
      case 0: // id
        satellites[writeCopy][satellites_i].id = tmp.toInt();
        break;
      case 1: // elevation from horizon in degrees
        satellites[writeCopy][satellites_i].elevation = tmp.toInt();
        break;
      case 2: // azimuth from clockwise north in degrees
        satellites[writeCopy][satellites_i].azimuth = tmp.toInt();
        break;
      case 3: // snr
        satellites[writeCopy][satellites_i].snr = tmp.toInt();
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
   
    tmp = "\0";
    msg = "$";
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
  msg += c;
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
    tmp = "\0";
    count_++;
  } else if (c == '\r' || c == '\n') {
    // carriage return, so check
    String checksum = tmp;
    String checkParity = String(parity_, HEX);
    checkParity.toUpperCase();

    validString = checkParity.equals(checksum);

    if (validString) {
      if(validCode == inTimeCode) {
        this->commit();
        isUpdated_ = true;
      }
      // commit datetime
    }

    // end of string
    msg = "\0";
    tmp = "\0";
    count_ = 0;
    parity_ = 0;
    validCode = waitDollar;
    return isUpdated_;
  } else {
    // ordinary char
    tmp += c;
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
