#include <string.h>
#include "test.h"
#include "BootloaderMagic.h"

static bool feedString(BootloaderMagic &m, const char *s) {
  bool triggered = false;
  for(size_t i = 0; s[i] != '\0'; i++) {
    triggered = m.feed(s[i]);
  }
  return triggered;
}

void test_full_magic_sequence_triggers() {
  BootloaderMagic m;
  TEST_ASSERT_TRUE(feedString(m, "rebootnow"));
}

void test_only_the_final_character_triggers() {
  BootloaderMagic m;
  const char *magic = "rebootnow";
  for(size_t i = 0; magic[i + 1] != '\0'; i++) {
    TEST_ASSERT_FALSE(m.feed(magic[i]));
  }
  TEST_ASSERT_TRUE(m.feed(magic[strlen(magic) - 1]));
}

void test_partial_prefix_does_not_trigger() {
  BootloaderMagic m;
  TEST_ASSERT_FALSE(feedString(m, "rebootno"));
}

void test_unrelated_garbage_does_not_trigger() {
  BootloaderMagic m;
  TEST_ASSERT_FALSE(feedString(m, "hello world, this is normal log output"));
}

void test_mismatch_recovers_and_can_still_trigger() {
  BootloaderMagic m;
  TEST_ASSERT_FALSE(feedString(m, "xyz"));
  TEST_ASSERT_TRUE(feedString(m, "rebootnow"));
}

// The mismatch-recovery branch (`matched_ = (c == magic_[0]) ? 1 : 0`)
// specifically handles a mismatching character that happens to equal the
// magic string's own first character ('r') -- e.g. "re" then "rebootnow"
// should still trigger, restarting the match at position 1 rather than 0,
// not require the whole sequence to restart from scratch.
void test_false_start_sharing_first_letter_still_triggers() {
  BootloaderMagic m;
  TEST_ASSERT_FALSE(feedString(m, "re"));
  TEST_ASSERT_TRUE(feedString(m, "bootnow"));
}

void test_can_trigger_more_than_once() {
  BootloaderMagic m;
  TEST_ASSERT_TRUE(feedString(m, "rebootnow"));
  TEST_ASSERT_TRUE(feedString(m, "rebootnow"));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_full_magic_sequence_triggers);
  RUN_TEST(test_only_the_final_character_triggers);
  RUN_TEST(test_partial_prefix_does_not_trigger);
  RUN_TEST(test_unrelated_garbage_does_not_trigger);
  RUN_TEST(test_mismatch_recovers_and_can_still_trigger);
  RUN_TEST(test_false_start_sharing_first_letter_still_triggers);
  RUN_TEST(test_can_trigger_more_than_once);
  return UNITY_END();
}
