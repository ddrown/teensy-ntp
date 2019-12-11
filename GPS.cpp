// from https://raw.githubusercontent.com/DennisSc/PPS-ntp-server/master/src/GPS.cpp

#include <HardwareSerial.h>
#include "DateTime.h"
#include "GPS.h"

// GPS init
#define RXPin  13
#define TXPin  15
#define GPSBaud 115200
#define gpsTimeOffset 2 //centisecond raw offset, compared to known-good stratum 1 server

#define GPS_CODE_ZDA "GPZDA"
 
/**
 * Save new date and time to private variables
 */
void GPSDateTime::commit() {
  time_ = newTime_;
  year_ = newYear_;
  month_ = newMonth_;
  day_ = newDay_;
  ltzh_ = newLtzh_;
  ltzn_ = newLtzn_;
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

uint32_t GPSDateTime::centisecond() {
  return time_ % 100;
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

void GPSDateTime::ltzh(String ltzh) {
  newLtzh_ = ltzh.toInt();
}
uint16_t GPSDateTime::ltzh(void) { return ltzh_; };

void GPSDateTime::ltzn(String ltzn) {
  newLtzn_ = ltzn.toInt();
}
uint16_t GPSDateTime::ltzn(void) { return ltzn_; };




/**
 * Send message
 * @param msg uint8_t array
 * @param len uint8_t
 */
void msendMessage(uint8_t *msg, uint8_t len, HardwareSerial port) 
{
  int i = 0;
  for (i = 0; i < len; i++) 
  {
    port.write(msg[i]);
    // Serial.print(msg[i], HEX);
  }
  port.println();
}


/**
 * Is acknowledge right from message?
 * @param  msg uint8_t array
 * @return     bool
 */
bool getAck(uint8_t *msg, HardwareSerial port) 
{
  uint8_t b;
  uint8_t ackByteID = 0;
  uint8_t ackPacket[10];
  unsigned long startTime = millis();
  Serial.print(" * Reading ACK response: ");

  // Construct the expected ACK packet
  ackPacket[0] = 0xB5;  // header
  ackPacket[1] = 0x62;  // header
  ackPacket[2] = 0x05;  // class
  ackPacket[3] = 0x01;  // id
  ackPacket[4] = 0x02;  // length
  ackPacket[5] = 0x00;
  ackPacket[6] = msg[2];  // ACK class
  ackPacket[7] = msg[3];  // ACK id
  ackPacket[8] = 0;   // CK_A
  ackPacket[9] = 0;   // CK_B

  // Calculate the checksums
  uint8_t i = 2;
  for (i = 2; i < 8; i++)
  {
    ackPacket[8] = ackPacket[8] + ackPacket[i];
    ackPacket[9] = ackPacket[9] + ackPacket[8];
  }

  while (1) 
  {
    // Test for success
    if (ackByteID > 9) 
    {
      // All packets in order!
      Serial.println(" (SUCCESS!)");
      return true;
    }

    // Timeout if no valid response in 3 seconds
    if (millis() - startTime > 3000) 
    {
      Serial.println(" (FAILED!)");
      return false;
    }

    // Make sure data is available to read
    if (port.available()) 
    {
      b = port.read();

      // Check that bytes arrive in sequence as per expected ACK packet
      if (b == ackPacket[ackByteID]) 
      {
        ackByteID++;
        //Serial.print(b, HEX);
      } else 
      {
        ackByteID = 0;  // Reset and look again, invalid order
      }
    }
  }
}

/**
 * Decode NMEA line to date and time
 * $GPZDA,174304.36,24,11,2015,00,00*66
 * $0    ,1        ,2 ,3 ,4   ,5 ,6 *7  <-- pos
 * @return line decoded
 */
bool GPSDateTime::decode() {
  char c = gpsUart_->read();

  if (c == '$') {
   
    tmp = "\0";
    msg = "$";
    count_ = 0;
    parity_ = 0;
    validCode = true;
    isNotChecked = true;
    isUpdated_ = false;
    return false;
  }

  if (!validCode) {
    return false;
  }
  msg += c;
  if (c == ',' || c == '*') {
    // determinator between values
    switch (count_) {
      case 0: // ID
        if (tmp.equals(GPS_CODE_ZDA)) {
          validCode = true;
        } else {
          validCode = false;
        }
        break;
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
      case 5:
        this->ltzh(tmp);
        break;
      case 6:
        this->ltzn(tmp);
        break;
      default:
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
  } else if (c == '\r') {
    // carriage return, so check
    String checksum = tmp;
    String checkParity = String(parity_, HEX);
    checkParity.toUpperCase();

    validString = checkParity.equals(checksum);

    if (validString) {
      this->commit();
      isUpdated_ = true;
      // commit datetime
    }
  } else if (c == '\n') {
    // end of string
    tmp = "\0";
    count_ = 0;
    parity_ = 0;
    return true;
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
  return DateTime(this->year(), this->month(), this->day(),
    this->hour(), this->minute(), this->second(), this->centisecond());
}
