#include "LeapSeconds.h"

// Historical leap seconds, in the same format as IERS/NIST's
// `leap-seconds.list`: NTP timestamp (seconds since 1900) at which the
// listed cumulative TAI-UTC offset takes effect, i.e. 00:00:00 UTC of the
// day after the leap second occurred. The 1972-01-01 entry establishes the
// initial 10s baseline when leap seconds were introduced rather than
// representing an inserted second itself, but is included for consistency
// with the authoritative source and because offsetAt() needs a starting
// value anyway.
const LeapSecondEntry leapSeconds[] = {
  { 2272060800UL, 10, LEAP_INSERT }, // 1972-01-01
  { 2287785600UL, 11, LEAP_INSERT }, // 1972-07-01
  { 2303683200UL, 12, LEAP_INSERT }, // 1973-01-01
  { 2335219200UL, 13, LEAP_INSERT }, // 1974-01-01
  { 2366755200UL, 14, LEAP_INSERT }, // 1975-01-01
  { 2398291200UL, 15, LEAP_INSERT }, // 1976-01-01
  { 2429913600UL, 16, LEAP_INSERT }, // 1977-01-01
  { 2461449600UL, 17, LEAP_INSERT }, // 1978-01-01
  { 2492985600UL, 18, LEAP_INSERT }, // 1979-01-01
  { 2524521600UL, 19, LEAP_INSERT }, // 1980-01-01
  { 2571782400UL, 20, LEAP_INSERT }, // 1981-07-01
  { 2603318400UL, 21, LEAP_INSERT }, // 1982-07-01
  { 2634854400UL, 22, LEAP_INSERT }, // 1983-07-01
  { 2698012800UL, 23, LEAP_INSERT }, // 1985-07-01
  { 2776982400UL, 24, LEAP_INSERT }, // 1988-01-01
  { 2840140800UL, 25, LEAP_INSERT }, // 1990-01-01
  { 2871676800UL, 26, LEAP_INSERT }, // 1991-01-01
  { 2918937600UL, 27, LEAP_INSERT }, // 1992-07-01
  { 2950473600UL, 28, LEAP_INSERT }, // 1993-07-01
  { 2982009600UL, 29, LEAP_INSERT }, // 1994-07-01
  { 3029443200UL, 30, LEAP_INSERT }, // 1996-01-01
  { 3076704000UL, 31, LEAP_INSERT }, // 1997-07-01
  { 3124137600UL, 32, LEAP_INSERT }, // 1999-01-01
  { 3345062400UL, 33, LEAP_INSERT }, // 2006-01-01
  { 3439756800UL, 34, LEAP_INSERT }, // 2009-01-01
  { 3550089600UL, 35, LEAP_INSERT }, // 2012-07-01
  { 3644697600UL, 36, LEAP_INSERT }, // 2015-07-01
  { 3692217600UL, 37, LEAP_INSERT }, // 2017-01-01
};
const uint8_t leapSecondsCount = sizeof(leapSeconds) / sizeof(leapSeconds[0]);

// Table is sorted ascending, so the entry in effect at ntpTime is the
// *last* one whose effectiveNtpTime is <= ntpTime -- scan from the end and
// return on the first match instead of scanning forward and having to
// remember the most recent qualifying entry seen so far.
int8_t leapSecondOffsetAt(uint32_t ntpTime) {
  for (int i = leapSecondsCount - 1; i >= 0; i--) {
    if (leapSeconds[i].effectiveNtpTime <= ntpTime) {
      return leapSeconds[i].cumulativeOffset;
    }
  }
  return 0;
}

bool leapSecondPendingToday(uint32_t ntpTime, LeapSecondType *type) {
  for (int i = leapSecondsCount - 1; i >= 0; i--) {
    uint32_t dayStart = leapSeconds[i].effectiveNtpTime - 86400;
    if (ntpTime >= dayStart && ntpTime < leapSeconds[i].effectiveNtpTime) {
      *type = leapSeconds[i].type;
      return true;
    }
  }
  return false;
}

int8_t leapSecondOffsetAtTai(uint32_t taiTime, bool *isLeapInstant) {
  int matchIndex = -1;
  for (int i = leapSecondsCount - 1; i >= 0; i--) {
    uint32_t taiBoundary = leapSeconds[i].effectiveNtpTime + leapSeconds[i].cumulativeOffset;
    if (taiBoundary <= taiTime) {
      matchIndex = i;
      break;
    }
  }
  int8_t offset = (matchIndex >= 0) ? leapSeconds[matchIndex].cumulativeOffset : 0;
  int nextIndex = matchIndex + 1;
  if (isLeapInstant) {
    *isLeapInstant = (nextIndex < leapSecondsCount) &&
      (leapSeconds[nextIndex].effectiveNtpTime + (uint32_t)leapSeconds[nextIndex].cumulativeOffset == taiTime + 1);
  }
  return offset;
}

uint32_t taiToWireNtp(uint32_t taiTime) {
  bool isLeapInstant;
  int8_t offset = leapSecondOffsetAtTai(taiTime, &isLeapInstant);
  return taiTime - offset;
}
