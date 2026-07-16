#pragma once

#include "NtpTimestamp.h"

class NTPClock {
  public:
    NTPClock() : timeset_(0), ppb_(0), refTime_(0) {};
    void setTime(uint32_t micros, TaiNtpTime ntpTimestamp);
    uint8_t getTime(TaiNtpTime *ntpTimestamp, uint32_t *ntpFractional);
    uint8_t getTime(uint32_t now, TaiNtpTime *ntpTimestamp, uint32_t *ntpFractional);
    int64_t getOffset(uint32_t now, TaiNtpTime ntpTimestamp, uint32_t ntpFractional);
    void setPpb(int32_t ppb);
    int32_t getPpb() { return ppb_; };
    TaiNtpTime getReftime() { return refTime_; };
    void setRefTime(TaiNtpTime refTime) { refTime_ = refTime; };

  private:
    uint8_t timeset_;
    // lastMicros_ local time, ntpTimestamp_ real time
    uint32_t lastMicros_;
    Ntp64 ntpTimestamp_, temp_;
    int32_t ppb_;
    TaiNtpTime refTime_;
};

extern NTPClock localClock;
