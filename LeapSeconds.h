#pragma once

#include <stdint.h>

// Compiled-in table of known leap seconds, the same idea as IERS/NIST's
// published `leap-seconds.list` (and in the same format: an NTP timestamp
// -- seconds since 1900, matching DateTime::ntptime()'s epoch -- paired
// with the cumulative TAI-UTC offset that takes effect at that moment).
// This exists because NMEA has no vendor-neutral way to signal an upcoming
// leap second (only proprietary extensions like u-blox's UBX-NAV-TIMELS),
// so rather than get this from the GPS receiver, consult a known table the
// same way ntpd/chrony/ntpsec do. See TODO.md, "Leap second handling".
//
// To add a newly-announced leap second: append one entry to leapSeconds[]
// below with its effective NTP timestamp (00:00:00 UTC of the day *after*
// the leap second occurs) and the new cumulative offset, then rebuild.
// IERS Bulletin C announces new ones with ~6 months' notice; there is
// deliberately no runtime/web mechanism to add one without a rebuild (see
// TODO.md for why).

enum LeapSecondType : uint8_t {
  LEAP_INSERT, // 61-second minute (the only kind that has ever happened)
  LEAP_DELETE, // 59-second minute (never happened; NTP_LEAP_59S exists for it)
};

struct LeapSecondEntry {
  uint32_t effectiveNtpTime; // NTP seconds (since 1900) the new offset takes effect
  int8_t cumulativeOffset;   // TAI-UTC offset in seconds, effective from here onward
  LeapSecondType type;
};

extern const LeapSecondEntry leapSeconds[];
extern const uint8_t leapSecondsCount;

// Cumulative TAI-UTC offset (seconds) in effect at ntpTime. Returns 0 if
// ntpTime is before the table's first known entry (1972 -- not a realistic
// operating date for this device, but a defined answer rather than
// undefined behavior).
int8_t leapSecondOffsetAt(uint32_t ntpTime);

// True if ntpTime falls within the UTC day a leap second is scheduled to
// occur -- i.e. the day immediately before some table entry's
// effectiveNtpTime. If true, *type is set to that entry's type.
bool leapSecondPendingToday(uint32_t ntpTime, LeapSecondType *type);
