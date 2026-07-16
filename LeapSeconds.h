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
//
// ntpTime here is in the real NTP *wire* format domain -- the same
// leap-second-naive "days*86400 + h*3600 + m*60 + s" count leapSeconds[]'s
// effectiveNtpTime values themselves are in (matching the real
// leap-seconds.list convention). Use this for anything that has to match
// what's actually sent over the wire (see leapSecondOffsetAtTai() below for
// the reverse-domain counterpart).
int8_t leapSecondOffsetAt(uint32_t ntpTime);

// True if ntpTime falls within the UTC day a leap second is scheduled to
// occur -- i.e. the day immediately before some table entry's
// effectiveNtpTime. If true, *type is set to that entry's type. Same wire
// NTP domain as leapSecondOffsetAt().
bool leapSecondPendingToday(uint32_t ntpTime, LeapSecondType *type);

// Cumulative TAI-UTC offset (seconds) in effect at taiTime, where taiTime is
// in the *TAI-like* domain instead: real, leap-second-adjusted seconds since
// 1900, monotonic with no aliasing across a leap second -- the domain
// DateTime::ntptime()/unixtime() return (see DateTime.h). This is the
// reverse-lookup counterpart to leapSecondOffsetAt(): each table entry's
// wire-domain effectiveNtpTime, plus the *new* offset that takes effect
// there, gives that entry's boundary in the TAI-like domain too, so this can
// scan the same table without approximation.
//
// If taiTime falls exactly on the leap second itself (the literal 23:59:60
// instant -- a real, distinct instant in this monotonic domain, even though
// the 32-bit wire format can't represent it as distinct from the following
// day's 00:00:00; that collision is exactly what the wire protocol's LI
// field exists to flag out-of-band instead), *isLeapInstant is set true and
// the offset returned is the one in effect *before* that boundary (the
// correct one to reconstruct 23:59:60 with, as opposed to 00:00:00 the
// following day, which shares the same wire-format value once converted).
int8_t leapSecondOffsetAtTai(uint32_t taiTime, bool *isLeapInstant);

// Converts a TAI-like timestamp (DateTime::ntptime()'s domain) back to real
// NTP wire format (UTC-seconds-since-1900) -- the domain the wire protocol,
// and anything writing an outgoing NTP packet, needs.
uint32_t taiToWireNtp(uint32_t taiTime);
