#pragma once

#include <stdint.h>
#include "NTPClock.h"
#include "ClockPID.h"

// If no GPS/PPS sample has been accepted (ClockDiscipline::process() called
// past the PPS/GPS lag check) in this long, presume GPS/PPS has stopped and
// enter holdover. A single transient PPS/GPS lag rejection (see the
// ppsToGPS > 950 check in updateTime(), which already tolerates this as
// normal jitter, not an outage) doubles the gap between accepted samples to
// ~2000ms on its own, and poll() runs on its own unsynchronized ~1Hz cycle
// from slower_poll(), adding up to another ~1000ms of phase jitter on top --
// 4000ms leaves comfortable margin above that combined worst case so one
// noisy fix cycle doesn't spuriously trip holdover.
#ifndef HOLDOVER_STALE_MS
#define HOLDOVER_STALE_MS 4000
#endif

// Bound on how large a single gap between poll() calls is trusted to
// accumulate into the holdover-duration estimate. poll() is expected to run
// roughly once/sec from slower_poll(); a gap bigger than this (a stalled
// loop(), not a GPS outage) is dropped rather than added, so a rare hiccup
// can't inflate the dispersion estimate by however long the hiccup lasted.
#ifndef HOLDOVER_POLL_MAX_GAP_MS
#define HOLDOVER_POLL_MAX_GAP_MS 5000
#endif

// Worst-case oscillator drift-rate uncertainty used to grow the estimated
// dispersion while in holdover, in parts-per-million -- same role as NTP's
// usual PHI constant (dispersion grows by PHI * elapsed).
#ifndef HOLDOVER_PHI_PPM
#define HOLDOVER_PHI_PPM 15.0
#endif

struct HoldoverStatus {
  bool inHoldover;
  uint32_t dispersion; // only meaningful when inHoldover is true
};

// Watches for GPS/PPS going silent and, once it has, takes over disciplining
// the clock: switches localClock's ppb correction from the full P+I+D output
// to the PID's D term alone (P and I are phase/accumulated-offset terms that
// go stale without fresh samples), and reports a dispersion estimate that
// grows with elapsed holdover time so NTPServer's existing
// dispersion>0x10000 check naturally falls back to stratum 16 if the outage
// runs long enough. See TODO.md, "GPS lock lost / PPS stopped".
class ClockHoldover {
  public:
    ClockHoldover(NTPClock *clock, ClockPID_c *pid) :
      localClock_(clock), pid_(pid), everSeen_(false), inHoldover_(false),
      lastGoodMillis_(0), lastPollMillis_(0), lastGoodDispersion_(0),
      holdoverElapsedMs_(0) {};

    // Call whenever ClockDiscipline::process() accepts a sample (i.e. after
    // every call, regardless of whether it resolved into a PID update) --
    // this is the "GPS/PPS is alive" signal that resets the staleness timer.
    void noteSampleReceived(uint32_t nowMillis);

    // Call whenever ClockDiscipline::process() resolves a new dispersion
    // value -- holdover grows its estimate from the last real value rather
    // than an arbitrary baseline.
    void noteDispersion(uint32_t dispersion) { lastGoodDispersion_ = dispersion; };

    // Call periodically (e.g. ~1/sec from slower_poll()). Returns whether
    // holdover is currently active and, if so, the dispersion value the
    // caller should push to the server; also drives localClock's ppb
    // directly while in holdover.
    HoldoverStatus poll(uint32_t nowMillis);

    bool inHoldover() { return inHoldover_; };

  private:
    NTPClock *localClock_;
    ClockPID_c *pid_;
    bool everSeen_;
    bool inHoldover_;
    uint32_t lastGoodMillis_;
    uint32_t lastPollMillis_;
    uint32_t lastGoodDispersion_;
    uint32_t holdoverElapsedMs_;
};

extern ClockHoldover holdover;
