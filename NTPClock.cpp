#include <Arduino.h>
#include "NTPClock.h"

void NTPClock::setTime(uint32_t micros, uint32_t ntpTimestamp) {
  timeset_ = 1;
  lastMicros_ = micros;
  ntpTimestamp_.units[NTPCLOCK_SECONDS] = ntpTimestamp;
  ntpTimestamp_.units[NTPCLOCK_FRACTIONAL] = 0;
}

uint8_t NTPClock::getTime(uint32_t *ntpTimestamp, uint32_t *ntpFractional) {
  uint32_t now = micros();

  return getTime(now, ntpTimestamp, ntpFractional);
}

uint8_t NTPClock::getTime(uint32_t now, uint32_t *ntpTimestamp, uint32_t *ntpFractional) {
  if (!timeset_)
    return 0;

  uint64_t ntpFracPassed = (now - lastMicros_) * 4294967296ULL / 1000000ULL;
  int32_t ntpFracPassedDrift = (int64_t)ntpFracPassed * ppb_ / 1000000000LL;
  ntpFracPassed += ntpFracPassedDrift;
  if(ntpFracPassed >= 4294967296ULL) {
    lastMicros_ = now;
    ntpTimestamp_.whole += ntpFracPassed;
    ntpFracPassed = 0;
  }
  if(ntpTimestamp != NULL)
    *ntpTimestamp = ntpTimestamp_.units[NTPCLOCK_SECONDS];
  if(ntpFractional != NULL)
    *ntpFractional = ntpTimestamp_.units[NTPCLOCK_FRACTIONAL] + ntpFracPassed;

  return 1;
}

void NTPClock::setPpb(int32_t ppb) {
  ppb_ = ppb;
}
