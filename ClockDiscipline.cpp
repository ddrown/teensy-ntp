#include <stdint.h>
#include "ClockDiscipline.h"

uint8_t ClockDiscipline::median(int64_t one, int64_t two, int64_t three) {
  if(one > two) {
    if(one > three) {
      if(two > three) {
        // 1 2 3
        return 2-1;
      } else {
        // 1 3 2
        return 3-1;
      }
    } else {
      // 3 1 2
      return 1-1;
    }
  } else {
    if(two > three) {
      if(one > three) {
        // 2 1 3
        return 1-1;
      } else {
        // 2 3 1
        return 3-1;
      }
    } else {
      // 3 2 1
      return 2-1;
    }
  }
}

uint32_t ClockDiscipline::ntp64_to_32(int64_t offset) {
  if(offset < 0)
    offset *= -1;
  // take 16bits off the bottom and top
  offset = offset >> 16;
  return offset & 0xffffffff;
}

DisciplineResult ClockDiscipline::process(uint32_t pps, TaiNtpTime gpstime) {
  DisciplineResult r = {};
  r.pps = pps;
  r.gpstime = gpstime;

  if(settime_) {
    if (gpstime.v < lastGpstime_.v) {
      // Backwards jump -- never valid, leap second or not. A real leap
      // second only ever produces a *stall* (the same gpstime again), never
      // something that looks like it went backwards -- this is the "GPS
      // module repeats an earlier second than the one before the leap
      // boundary" failure mode, a vendor bug rather than a documented
      // behavior. See TODO.md, "Leap second handling".
      r.rejected = true;
      return r;
    }
    if (gpstime.v == lastGpstime_.v) {
      LeapSecondType stallType;
      if (leapSecondStallSecond(taiToWireNtp(gpstime), &stallType)) {
        // The GPS receiver stalled on the last regular second instead of
        // emitting a literal :60 (a real, observed receiver behavior) --
        // step forward by one full second, matching what a :60-emitting
        // receiver would have produced natively.
        gpstime = TaiNtpTime(gpstime.v + 1);
        r.gpstime = gpstime;
        r.leapSecondCorrected = true;
        // This sample's offset is genuinely anomalous by about a second --
        // nothing steps localClock_ across the inserted second, so it's a
        // real (if expected) discontinuity, not drift. In steady state
        // that's harmless: ClockPID's own median-of-3 upstream filters one
        // outlier against its neighbors before it ever reaches the
        // regression. During bootstrap there's no such protection (every
        // sample resolves immediately), so the anomaly goes straight into
        // ClockPID's 16-entry window and corrupts it for several resolves --
        // confirmed on the bench (dChiSq overflow, ppb pinned to
        // NTPClock::setPpb()'s clamp) with a short lead-in, absent with a
        // long one. Reset just this narrower case rather than
        // unconditionally, so an ordinary leap second during steady-state
        // operation doesn't needlessly throw away a perfectly good
        // regression history. See TODO.md, "Leap second handling".
        if (!pid_->full()) {
          pid_->reset_clock();
        }
      } else {
        // A duplicate unrelated to a leap second -- a GPS glitch or a
        // repeated fix. Reject it the same way an out-of-tolerance lag
        // sample already gets rejected before process() is even called.
        r.rejected = true;
        return r;
      }
    }
    lastGpstime_ = gpstime;

    int64_t offset = localClock_->getOffset(pps, gpstime, 0);
    samples_[wait_].offset = offset;
    samples_[wait_].pps = pps;
    samples_[wait_].gpstime = gpstime;
    if(pid_->full() && wait_) {
      wait_--;
    } else {
      uint8_t median_index = wait_;
      if(wait_ == 0) {
        median_index = median(samples_[0].offset, samples_[1].offset, samples_[2].offset);
      }
      pid_->add_sample(samples_[median_index].pps, samples_[median_index].gpstime, samples_[median_index].offset);
      localClock_->setRefTime(samples_[median_index].gpstime);
      float ppb = pid_->out() * 1000000000.0;
      localClock_->setPpb(ppb);
      wait_ = DISCIPLINE_WAIT_COUNT-1; // (2+1)*16=48s, 80MHz wraps at 53s

      // TODO: this should grow when out of sync
      r.updated = true;
      r.pps = samples_[median_index].pps;
      r.gpstime = samples_[median_index].gpstime;
      r.offset = samples_[median_index].offset;
      r.ppb = ppb;
      r.dispersion = ntp64_to_32(samples_[median_index].offset);
    }
  } else {
    localClock_->setTime(pps, gpstime);
    pid_->add_sample(pps, gpstime, 0);
    settime_ = 1;
    lastGpstime_ = gpstime;
    r.clockSet = true;
  }

  return r;
}
