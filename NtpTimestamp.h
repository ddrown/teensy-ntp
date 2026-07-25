#pragma once

#include <stdint.h>

// Distinct wrapper types for the two NTP-domain seconds-since-1900
// conventions this codebase mixes, so the compiler catches a value from one
// domain being used where the other is expected instead of a human having
// to. See TODO.md, "Leap second handling" -- the two are numerically close
// (differ only by the current leap-second cumulative offset, ~37s) so a
// mixup doesn't produce an obviously-wrong number, just a silently wrong
// one, which is exactly the kind of bug type safety is good at catching and
// review is bad at catching.
//
// Deliberately minimal: just a tagged uint32_t (explicit single-arg
// constructor, so nothing converts to one of these by accident), no
// arithmetic or comparison operators. Any math needed at a given call site
// (add/subtract a duration, compare two same-domain instants) goes through
// the public `.v` field explicitly -- that's a deliberate, visible "this is
// raw-value math" moment rather than something hidden behind an operator
// overload. There is intentionally no conversion between the two types;
// cross the boundary via LeapSeconds.h's taiToWireNtp()/
// leapSecondOffsetAtTai().

// Real NTP wire format: leap-second-naive (every day is exactly 86400
// seconds), what's actually sent/received on the network, and what
// LeapSeconds.h's table (leapSeconds[].effectiveNtpTime) is keyed on.
struct WireNtpTime {
  uint32_t v;
  // Split from a single defaulted-argument constructor: a defaulted
  // explicit constructor doubling as the default constructor causes GCC to
  // warn ("would use explicit constructor") anywhere a containing struct is
  // value-initialized with `{}`/`= {}` (DisciplineResult, HoldoverStatus,
  // etc.) -- harmless, but noisy. Two constructors instead of one sidesteps
  // it: `{}` picks the plain default constructor, WireNtpTime(x) still
  // requires an explicit call.
  WireNtpTime() : v(0) {}
  explicit WireNtpTime(uint32_t seconds) : v(seconds) {}
};

// TAI-like: real, leap-second-adjusted seconds, monotonic across a leap
// second insertion/deletion -- DateTime::ntptime()'s domain, and what
// NTPClock/ClockPID/ClockDiscipline work in internally (see DateTime.h).
struct TaiNtpTime {
  uint32_t v;
  // See WireNtpTime's constructors above for why this is split in two.
  TaiNtpTime() : v(0) {}
  explicit TaiNtpTime(uint32_t seconds) : v(seconds) {}
};

// depends on CPU byte order -- matches the pre-existing NTPCLOCK_SECONDS/
// NTPCLOCK_FRACTIONAL convention this replaces/shares.
#define NTP64_SECONDS 1
#define NTP64_FRACTIONAL 0

// Packs a 32-bit whole-seconds + 32-bit fractional-seconds NTP timestamp
// into one 64-bit value so a small sub-second correction (a hardware
// latency adjustment, etc.) can be applied with plain 64-bit addition
// instead of hand-rolled carry logic. Named, shared replacement for the
// union{parts[2],whole} pattern previously duplicated in NTPClock.h and
// NTPServer.cpp.
//
// Domain-agnostic on purpose: it doesn't know or care whether its seconds
// half is a WireNtpTime or a TaiNtpTime value -- callers wrap/unwrap that
// via `.v` at the point they actually set or read it, same as anywhere
// else these types are used.
union Ntp64 {
  uint32_t units[2];
  uint64_t whole;

  uint32_t seconds() const { return units[NTP64_SECONDS]; }
  uint32_t fractional() const { return units[NTP64_FRACTIONAL]; }
  void setSeconds(uint32_t s) { units[NTP64_SECONDS] = s; }
  void setFractional(uint32_t f) { units[NTP64_FRACTIONAL] = f; }
};
