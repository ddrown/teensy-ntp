#pragma once

// depends on CPU byte order
#define NTPCLOCK_SECONDS 1
#define NTPCLOCK_FRACTIONAL 0

class NTPClock {
  public:
    NTPClock() : timeset_(0), ppb_(0), refTime_(0) {};
    void setTime(uint32_t micros, uint32_t ntpTimestamp);
    uint8_t getTime(uint32_t *ntpTimestamp, uint32_t *ntpFractional);
    uint8_t getTime(uint32_t now, uint32_t *ntpTimestamp, uint32_t *ntpFractional);
    int64_t getOffset(uint32_t now, uint32_t ntpTimestamp, uint32_t ntpFractional);
    void setPpb(int32_t ppb);
    int32_t getPpb() { return ppb_; };
    uint32_t getReftime() { return refTime_; };
    void setRefTime(uint32_t refTime) { refTime_ = refTime; };

  private:
    uint8_t timeset_;
    // lastMicros_ local time, ntpTimestamp_ real time
    uint32_t lastMicros_;
    union {
      uint32_t units[2];
      uint64_t whole;
    } ntpTimestamp_, temp_;
    int32_t ppb_;
    uint32_t refTime_;
};
