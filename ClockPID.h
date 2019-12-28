#pragma once

#include <stdint.h>

#ifndef NTPPID_KP
#define NTPPID_KP 0.001
#endif

#ifndef NTPPID_KI
#define NTPPID_KI 0.001
#endif

#ifndef NTPPID_KD
#define NTPPID_KD 1.0
#endif

#define NTPPID_MAX_COUNT 16

extern "C" {
  struct deriv_calc {
    float d, d_chisq;
  };

  struct linear_result {
    float a, b;
  };
}

class ClockPID_c {
  public:
    ClockPID_c() { count = 0; }
    float add_sample(uint32_t timestamp, uint32_t realSecond, int64_t corrected_offset);

    float p() { return last_p; };
    float i() { return last_i; };
    float d() { return last_d.d; };
    float d_chi() { return last_d.d_chisq; };
    float p_out() { return last_out_p; };
    float i_out() { return last_out_i; };
    float d_out() { return last_out_d; };
    float out() { return last_out; };
    uint32_t samples() { return count; };
    bool full() { return count == NTPPID_MAX_COUNT; };

    void reset_clock() { count = 0; }

  private:
    uint32_t timestamps[NTPPID_MAX_COUNT]; // in microseconds
    uint32_t realSeconds[NTPPID_MAX_COUNT]; // actual seconds
    int32_t rawOffsets[NTPPID_MAX_COUNT]; // in microseconds
    int64_t corrected_offsets[NTPPID_MAX_COUNT]; // in fractional ntp seconds

    uint32_t count;

    float last_p, last_i;
    struct deriv_calc last_d;
    float last_out_p, last_out_i, last_out_d, last_out;

    struct linear_result theil_sen(float avg_ts, float avg_out);
    float chisq(struct linear_result lin, uint32_t *timestampDurations);
    struct deriv_calc calculate_d();
    float calculate_i();
    float limit_500(float factor);
    void make_room();
    float average(int32_t *points);
    float average(uint32_t *points);
};

extern ClockPID_c ClockPID;
