#include "test.h"
#include "NTPResponseFields.h"

void test_request_length_valid_at_exact_packet_size() {
  TEST_ASSERT_TRUE(ntpRequestLengthIsValid(sizeof(struct ntp_packet)));
}

void test_request_length_valid_when_larger() {
  TEST_ASSERT_TRUE(ntpRequestLengthIsValid(sizeof(struct ntp_packet) + 8));
}

void test_request_length_invalid_when_smaller() {
  TEST_ASSERT_FALSE(ntpRequestLengthIsValid(sizeof(struct ntp_packet) - 1));
}

void test_request_version_and_mode_valid_range() {
  TEST_ASSERT_TRUE(ntpRequestVersionAndModeAreValid(2, NTP_MODE_CLIENT));
  TEST_ASSERT_TRUE(ntpRequestVersionAndModeAreValid(3, NTP_MODE_CLIENT));
  TEST_ASSERT_TRUE(ntpRequestVersionAndModeAreValid(4, NTP_MODE_CLIENT));
}

void test_request_version_out_of_range_rejected() {
  TEST_ASSERT_FALSE(ntpRequestVersionAndModeAreValid(1, NTP_MODE_CLIENT));
  TEST_ASSERT_FALSE(ntpRequestVersionAndModeAreValid(5, NTP_MODE_CLIENT));
}

void test_request_wrong_mode_rejected() {
  TEST_ASSERT_FALSE(ntpRequestVersionAndModeAreValid(4, NTP_MODE_SERVER));
  TEST_ASSERT_FALSE(ntpRequestVersionAndModeAreValid(4, NTP_MODE_SYMACT));
}

void test_header_unsynced_when_reftime_zero() {
  NTPResponseHeader h = selectNTPResponseHeader(TaiNtpTime(0), 0);
  TEST_ASSERT_EQUAL(16, h.stratum);
  TEST_ASSERT_EQUAL(0, h.ident);
  TEST_ASSERT_EQUAL(NTP_LEAP_UNSYNC, h.leap);
}

// A valid reftime doesn't matter if dispersion is over the 1-second
// (0x10000, NTP-short fixed point) threshold -- still falls back to
// stratum 16/unsynced, same as NTPServer::recv()'s original inline check.
void test_header_unsynced_when_dispersion_over_threshold() {
  NTPResponseHeader h = selectNTPResponseHeader(TaiNtpTime(3800000000UL), 0x10001);
  TEST_ASSERT_EQUAL(16, h.stratum);
  TEST_ASSERT_EQUAL(0, h.ident);
  TEST_ASSERT_EQUAL(NTP_LEAP_UNSYNC, h.leap);
}

void test_header_dispersion_at_threshold_is_still_synced() {
  // 0x10000 itself is not "over" the threshold.
  NTPResponseHeader h = selectNTPResponseHeader(TaiNtpTime(3800000000UL), 0x10000);
  TEST_ASSERT_EQUAL(1, h.stratum);
}

void test_header_synced_no_leap_pending() {
  // Well clear of the 2017-01-01 leap second, no pending entry.
  NTPResponseHeader h = selectNTPResponseHeader(TaiNtpTime(3800000000UL), 0);
  TEST_ASSERT_EQUAL(1, h.stratum);
  TEST_ASSERT_EQUAL(0x50505300, h.ident);
  TEST_ASSERT_EQUAL(NTP_LEAP_NONE, h.leap);
}

// 2016-12-31 (TAI-domain, i.e. already leap-second-adjusted) is within the
// UTC day the real 2017-01-01 leap second was scheduled -- same fixture
// used in test-LeapSeconds.cpp's test_pending_today_true_at_start_of_leap_second_day
// (wire 3692217600 - 86400), converted to this function's TAI-domain input.
void test_header_leap_insert_pending() {
  // wire boundary (3692217600) - 43200 (halfway through the leap day) +
  // 36 (old offset, still in effect before the boundary) = TAI domain.
  TaiNtpTime midLeapDay(3692217600UL - 43200 + 36);
  NTPResponseHeader h = selectNTPResponseHeader(midLeapDay, 0);
  TEST_ASSERT_EQUAL(1, h.stratum);
  TEST_ASSERT_EQUAL(NTP_LEAP_61S, h.leap);
}

void test_clamp_poll_under_limit_unchanged() {
  TEST_ASSERT_EQUAL(5, clampNTPPoll(5));
}

void test_clamp_poll_at_limit_unchanged() {
  TEST_ASSERT_EQUAL(12, clampNTPPoll(12));
}

void test_clamp_poll_over_limit_clamped() {
  TEST_ASSERT_EQUAL(12, clampNTPPoll(200));
}

void test_wire_timestamp_from_tai_normal_instant() {
  // Well clear of any boundary, no latency correction -- isolates the pure
  // domain conversion (see test-LeapSeconds.cpp's test_tai_to_wire_ntp_normal_second
  // for the same reference value).
  NTPWireTimestamp t = ntpWireTimestampFromTai(TaiNtpTime(3800000037UL), 12345, 0);
  TEST_ASSERT_EQUAL(3800000000UL, t.seconds.v);
  TEST_ASSERT_EQUAL(12345, t.fractional);
}

// Applying a latency correction that carries the fractional part into the
// next second must still land on the correct wire-domain second.
void test_wire_timestamp_from_tai_fractional_carry() {
  NTPWireTimestamp t = ntpWireTimestampFromTai(TaiNtpTime(3800000000UL), 4294967200UL, 200);
  TEST_ASSERT_EQUAL(3800000001UL - 37, t.seconds.v);
  TEST_ASSERT_EQUAL(104, t.fractional);
}

// The instant a real leap second occurs (2016-12-31 23:59:60, TAI-domain --
// see test-LeapSeconds.cpp's test_tai_to_wire_ntp_leap_instant_and_next_day_collide_on_wire)
// must still convert to the correct wire-domain day, using the *old*
// (pre-boundary) offset -- this is the exact conversion recv() applies to
// every RX/TX timestamp, so it's worth covering the boundary here too, not
// just in LeapSeconds' own tests.
void test_wire_timestamp_from_tai_at_leap_instant() {
  NTPWireTimestamp t = ntpWireTimestampFromTai(TaiNtpTime(3692217637UL - 1), 0, 0);
  TEST_ASSERT_EQUAL(3692217600UL, t.seconds.v);
}

void test_wire_timestamp_from_wire_no_domain_conversion() {
  // Already wire format (as stored by NTPClients::addTx()) -- only the
  // latency correction applies, no taiToWireNtp() conversion.
  NTPWireTimestamp t = ntpWireTimestampFromWire(WireNtpTime(3692217637UL), 100, 50);
  TEST_ASSERT_EQUAL(3692217637UL, t.seconds.v);
  TEST_ASSERT_EQUAL(150, t.fractional);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_request_length_valid_at_exact_packet_size);
  RUN_TEST(test_request_length_valid_when_larger);
  RUN_TEST(test_request_length_invalid_when_smaller);
  RUN_TEST(test_request_version_and_mode_valid_range);
  RUN_TEST(test_request_version_out_of_range_rejected);
  RUN_TEST(test_request_wrong_mode_rejected);
  RUN_TEST(test_header_unsynced_when_reftime_zero);
  RUN_TEST(test_header_unsynced_when_dispersion_over_threshold);
  RUN_TEST(test_header_dispersion_at_threshold_is_still_synced);
  RUN_TEST(test_header_synced_no_leap_pending);
  RUN_TEST(test_header_leap_insert_pending);
  RUN_TEST(test_clamp_poll_under_limit_unchanged);
  RUN_TEST(test_clamp_poll_at_limit_unchanged);
  RUN_TEST(test_clamp_poll_over_limit_clamped);
  RUN_TEST(test_wire_timestamp_from_tai_normal_instant);
  RUN_TEST(test_wire_timestamp_from_tai_fractional_carry);
  RUN_TEST(test_wire_timestamp_from_tai_at_leap_instant);
  RUN_TEST(test_wire_timestamp_from_wire_no_domain_conversion);
  return UNITY_END();
}
