#include <Arduino.h>
#include "NTPClock.h"
#include "platform-clock.h"

void NTPClock::setTime(uint32_t micros, uint32_t ntpTimestamp) {
  timeset_ = 1;
  lastMicros_ = micros;
  ntpTimestamp_.units[NTPCLOCK_SECONDS] = ntpTimestamp;
  ntpTimestamp_.units[NTPCLOCK_FRACTIONAL] = 0;
}

uint8_t NTPClock::getTime(uint32_t *ntpTimestamp, uint32_t *ntpFractional) {
  uint32_t now = COUNTERFUNC();

  return getTime(now, ntpTimestamp, ntpFractional);
}

// takes around 28us on an esp8266 80MHz
uint8_t NTPClock::getTime(uint32_t now, uint32_t *ntpTimestamp, uint32_t *ntpFractional) {
  if (!timeset_)
    return 0;

  int64_t ntpFracPassed = (now - lastMicros_) * 4294967296LL / (int64_t)COUNTSPERSECOND;
  int32_t ntpFracPassedDrift = ntpFracPassed * ppb_ / 1000000000LL;
  ntpFracPassed += ntpFracPassedDrift;
  temp_.whole = ntpTimestamp_.whole + ntpFracPassed;
  if(ntpFracPassed >= 4294967296LL) { // every second
    lastMicros_ = now;
    ntpTimestamp_.whole = temp_.whole;
  }
  if(ntpTimestamp != NULL)
    *ntpTimestamp = temp_.units[NTPCLOCK_SECONDS];
  if(ntpFractional != NULL)
    *ntpFractional = temp_.units[NTPCLOCK_FRACTIONAL];

  return 1;
}

// returns + for local slower, - for local faster
// takes around 33us on an esp8266 80MHz
int64_t NTPClock::getOffset(uint32_t now, uint32_t ntpTimestamp, uint32_t ntpFractional) {
  uint32_t localS, localFS;
  if (getTime(now, &localS, &localFS) != 1) {
    return 0;
  }

  int32_t diffS = ntpTimestamp - localS; // assumption: clocks are within 2^31s
  int64_t diffFS = (int64_t)ntpFractional - localFS;
  diffFS += diffS * 4294967296LL;

  return diffFS;
}

// use + for local slower, - for local faster
// can be set once per second
void NTPClock::setPpb(int32_t ppb) {
  if(ppb >= -500000 && ppb <= 500000) { // limited to +/-500ppm
    ppb_ = ppb;
  }
}
