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

DisciplineResult ClockDiscipline::process(uint32_t pps, uint32_t gpstime) {
  DisciplineResult r = {};
  r.pps = pps;
  r.gpstime = gpstime;

  if(settime_) {
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
    r.clockSet = true;
  }

  return r;
}
