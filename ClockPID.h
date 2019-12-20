#pragma once

#include <stdint.h>

#define NTPPID_KP 0.001
#define NTPPID_KI 0.0001
#define NTPPID_KD 1.0

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
    float add_sample(uint32_t timestamp, int32_t raw_offset, int64_t corrected_offset);

    float p() { return last_p; };
    float i() { return last_i; };
    float d() { return last_d.d; };
    float d_chi() { return last_d.d_chisq; };
    float p_out() { return last_out_p; };
    float i_out() { return last_out_i; };
    float d_out() { return last_out_d; };
    float out() { return last_out; };

    void reset_clock() { count = 0; } // TODO: change all timestamps to negative instead of throwing them away

  private:
    uint32_t timestamps[NTPPID_MAX_COUNT]; // in microseconds
    int32_t raw_offsets[NTPPID_MAX_COUNT]; // interval(remote) - interval(local), in microseconds
    int64_t corrected_offsets[NTPPID_MAX_COUNT]; // in fractional ntp seconds

    uint32_t count;

    float last_p, last_i;
    struct deriv_calc last_d;
    float last_out_p, last_out_i, last_out_d, last_out;

    struct linear_result theil_sen(float avg_ts, float avg_out);
    float chisq(struct linear_result lin);
    struct deriv_calc calculate_d();
    float calculate_i();
    float limit_500(float factor);
    void make_room();
    float average(int32_t *points);
    float average(uint32_t *points);
};

extern ClockPID_c ClockPID;