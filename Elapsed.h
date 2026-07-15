#pragma once

#include <stdint.h>

// Wraparound-safe elapsed-time check for free-running 32-bit counters
// (millis(), the 1588/COUNTERFUNC() hardware counter, etc.), which wrap
// after 2^32 counter units. Plain `now - then` unsigned subtraction is only
// trustworthy while the true elapsed gap is under 2^32 units *and* the
// caller can bound how large a "real" gap is expected to be -- a genuinely
// large gap and a fully-wrapped-around one are otherwise indistinguishable
// from the pair (now, then) alone. Callers supply an explicit maxWindow for
// their domain (e.g. ~1s for the PPS/GPS lag check, much less than the
// ~53s period for the 1588 timer) so an implausibly large gap is reported
// as untrustworthy instead of silently being accepted.
//
// This does *not* make truly unbounded outages safe to detect from a
// single (now, then) pair -- an outage long enough to wrap the counter one
// or more extra times looks identical to a short one. Callers tracking an
// open-ended outage (e.g. GPS holdover) must poll often enough (much more
// often than maxWindow) and accumulate elapsed time incrementally, rather
// than taking one subtraction across the whole outage.
//
// Returns true and sets *elapsedOut to the forward gap from `then` to
// `now` if that gap is within [0, maxWindow]; returns false (leaving
// *elapsedOut untouched) otherwise -- including when `then` is actually
// "ahead of" now, which wraps around to a huge gap and is correctly
// rejected as implausible.
bool elapsedWithin(uint32_t now, uint32_t then, uint32_t maxWindow, uint32_t *elapsedOut);

// Saturating add for millisecond-domain accumulators that track an
// open-ended duration (e.g. total time spent in an ongoing outage) by
// summing bounded elapsedWithin() ticks. A plain `a + b` would wrap back to
// a small value once the running total passes UINT32_MAX ms (~49.7 days),
// which for a duration that's meant to keep growing is the same silent
// "looks fresh again" failure elapsedWithin() exists to prevent -- this
// pins at UINT32_MAX instead of wrapping.
uint32_t saturatingAddMs(uint32_t a, uint32_t b);
