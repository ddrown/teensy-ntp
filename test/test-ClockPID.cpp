#include "test.h"
#include "ClockPID.h"

void test_addsample() {
  ClockPID.reset_clock();
  float expected[NTPPID_MAX_COUNT] = {
    0.000000000,
    0.000005010,
    0.000005025,
    0.000005045,
    0.000005070,
    0.000005100,
    0.000005135,
    0.000005175,
    0.000005220,
    0.000005270,
    0.000005325,
    0.000005385,
    0.000005450,
    0.000005520,
    0.000005595,
    0.000005675
  };

  for(int i = 0; i < NTPPID_MAX_COUNT; i++) {
    // every second, clock drifts 5us and is uncorrected
    float sample = ClockPID.add_sample(999995*i, i, 21474.836480*i);
    TEST_ASSERT_FLOAT_WITHIN(1e-9, expected[i], sample);
    if(i > 1) {
      TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.000005, ClockPID.d());
      TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, ClockPID.d_chi());
    }
  }
}

void test_realsamples() {
  ClockPID.reset_clock();

  ClockPID.add_sample(838698, 3785790043, 0);

  ClockPID.add_sample(120838782, 3785790163, -16493);
  // -84/(120838782-838698) = -.000000699
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.699e-6, ClockPID.d());

  ClockPID.add_sample(121838782, 3785790164, -13624);
  // -84/(121838782-838698) = -.000000694
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.694e-6, ClockPID.d());

  ClockPID.add_sample(220838856, 3785790263, -47416);
  // -158/(221838856-838698) = -.000000718
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.700e-6, ClockPID.d());

  ClockPID.add_sample(221838856, 3785790264, -44547);
  // -158/(221838856-838698) = -.000000714
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.714e-6, ClockPID.d());
}

void test_counterwrap() {
  ClockPID.reset_clock();

  ClockPID.add_sample(4294805994, 3785790043, 0);

  ClockPID.add_sample(119838782, 3785790163, -16493);
  // -84/(120838782-838698) = -.000000699
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.699e-6, ClockPID.d());

  ClockPID.add_sample(120838782, 3785790164, -13624);
  // -84/(121838782-838698) = -.000000694
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.694e-6, ClockPID.d());

  ClockPID.add_sample(219838856, 3785790263, -47416);
  // -158/(221838856-838698) = -.000000718
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.700e-6, ClockPID.d());

  ClockPID.add_sample(220838856, 3785790264, -44547);
  // -158/(221838856-838698) = -.000000714
  TEST_ASSERT_FLOAT_WITHIN(1e-9, -0.714e-6, ClockPID.d());
}

void test_realvalues2() {
  ClockPID.reset_clock();

  struct {
    uint32_t micros;
    double offset;
    int32_t ppb;
  } offsets[] = {
    {250107536, 0.000001972, -987},
    {251107537, 0.000001959, -987},
    {252107538, 0.000001946, -987},
    {253107539, 0.000001933, -986},
    {254107539, 0.000002760, -985},
    {255107540, 0.000002585, -984},
    {256107541, 0.000002408, -982},
    {257107542, 0.000002066, -981},
    {258107543, 0.000001884, -897},
    {259107544, -0.000011995, -903},
    {260107545, -0.000011102, -918},
    {261107546, -0.000008694, -930},
    {262107547, -0.000006760, -937},
    {263107548, -0.000005647, -955},
    {264107549, -0.000002650, -1025},
    {265107550, 0.000009275, -1009},
    {266107551, 0.000007924, -1004},
    {267107552, 0.000006903, -1000},
    {268107552, 0.000007044, -995},
    {269107553, 0.000006175, -992},
    {270107554, 0.000005472, -990},
    {271107555, 0.000004938, -988},
    {272107556, 0.000004576, -986},
    {273107557, 0.000004210, -918},
    {274107558, -0.000008220, -930},
    {275107559, -0.000008118, -926},
    {276107560, -0.000008924, -924},
    {277107561, -0.000009372, -936},
    {278107562, -0.000007432, -1001},
    {279107563, 0.000004890, -978},
    {280107564, 0.000002652, -975},
    {281107564, 0.000002699, -982},
    {282107565, 0.000003992, -985},
    {283107566, -1.999995268, -500000}
  };

  uint32_t time = 3785882286;
  for(uint32_t i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
    int64_t corrected = offsets[i].offset * (double)4294967296.0;
    float sample = ClockPID.add_sample(offsets[i].micros, time+i, corrected);
    if(i > 14) {
      TEST_ASSERT_FLOAT_WITHIN(1e-9, offsets[i].ppb/1.0e9, sample);
    }
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_addsample);
  RUN_TEST(test_realsamples);
  RUN_TEST(test_counterwrap);
  RUN_TEST(test_realvalues2);
  return UNITY_END();
}
