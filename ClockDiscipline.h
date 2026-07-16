#pragma once

#include <stdint.h>
#include "NTPClock.h"
#include "ClockPID.h"

#define DISCIPLINE_WAIT_COUNT 3

struct DisciplineResult {
  bool clockSet;    // true: this call established the initial clock reference
  bool updated;     // true: a PID sample was committed (median-of-3 resolved)
  uint32_t pps;
  TaiNtpTime gpstime;
  int64_t offset;
  float ppb;
  uint32_t dispersion;
};

// Owns the median-of-3 outlier rejection and PID feeding that used to live
// inline in teensy-ntp.ino's updateTime(), so it can be unit tested on the
// host instead of only on-device.
class ClockDiscipline {
  public:
    ClockDiscipline(NTPClock *clock, ClockPID_c *pid) :
      localClock_(clock), pid_(pid), settime_(0), wait_(DISCIPLINE_WAIT_COUNT-1) {};

    // Feed one GPS-timestamped PPS sample; returns what (if anything) the
    // caller should do with it (report a clock-set message, or push new
    // dispersion/reftime/ppb values out to the server/local clock).
    DisciplineResult process(uint32_t pps, TaiNtpTime gpstime);

  private:
    NTPClock *localClock_;
    ClockPID_c *pid_;
    uint8_t settime_;
    uint8_t wait_;
    struct {
      int64_t offset;
      uint32_t pps;
      TaiNtpTime gpstime;
    } samples_[DISCIPLINE_WAIT_COUNT];

    static uint8_t median(int64_t one, int64_t two, int64_t three);
    static uint32_t ntp64_to_32(int64_t offset);
};

extern ClockDiscipline discipline;
