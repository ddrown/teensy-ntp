#include "test.h"
#include "Elapsed.h"

void test_normal_gap_within_window() {
  uint32_t elapsed = 0xdeadbeef;
  TEST_ASSERT_TRUE(elapsedWithin(1000, 900, 1000, &elapsed));
  TEST_ASSERT_EQUAL(100, elapsed);
}

void test_zero_gap() {
  uint32_t elapsed = 0xdeadbeef;
  TEST_ASSERT_TRUE(elapsedWithin(500, 500, 1000, &elapsed));
  TEST_ASSERT_EQUAL(0, elapsed);
}

void test_gap_exactly_at_window_boundary_is_valid() {
  uint32_t elapsed = 0;
  TEST_ASSERT_TRUE(elapsedWithin(2000, 1000, 1000, &elapsed));
  TEST_ASSERT_EQUAL(1000, elapsed);
}

void test_gap_just_past_window_is_rejected() {
  uint32_t elapsed = 0xdeadbeef;
  TEST_ASSERT_FALSE(elapsedWithin(2001, 1000, 1000, &elapsed));
  TEST_ASSERT_EQUAL(0xdeadbeef, elapsed); // untouched on rejection
}

// then is near the top of the 32-bit range, now has wrapped around to just
// past zero -- a real, short gap spanning a millis()/1588-counter wrap.
// This is exactly the case a naive `now - then` comparison against a fixed
// threshold gets right too (unsigned subtraction already wraps correctly);
// the point of this helper is the *rejection* cases below, not this one --
// but it's worth pinning down that the common case still works.
void test_wraparound_short_gap_is_still_valid() {
  uint32_t elapsed = 0;
  TEST_ASSERT_TRUE(elapsedWithin(0x00000010, 0xFFFFFFF0, 1000, &elapsed));
  TEST_ASSERT_EQUAL(32, elapsed);
}

// then is 100 ticks "ahead of" now -- not a valid forward gap. Unsigned
// subtraction wraps this to a huge number rather than a small negative
// one; maxWindow must reject it rather than accept it as a tiny elapsed
// time.
void test_then_after_now_is_rejected() {
  uint32_t elapsed = 0xdeadbeef;
  TEST_ASSERT_FALSE(elapsedWithin(100, 200, 1000, &elapsed));
  TEST_ASSERT_EQUAL(0xdeadbeef, elapsed);
}

void test_long_outage_exceeds_window() {
  uint32_t elapsed = 0xdeadbeef;
  TEST_ASSERT_FALSE(elapsedWithin(2000000, 100, 1000, &elapsed));
  TEST_ASSERT_EQUAL(0xdeadbeef, elapsed);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_normal_gap_within_window);
  RUN_TEST(test_zero_gap);
  RUN_TEST(test_gap_exactly_at_window_boundary_is_valid);
  RUN_TEST(test_gap_just_past_window_is_rejected);
  RUN_TEST(test_wraparound_short_gap_is_still_valid);
  RUN_TEST(test_then_after_now_is_rejected);
  RUN_TEST(test_long_outage_exceeds_window);
  return UNITY_END();
}
