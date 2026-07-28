#include "test.h"
#include "NTPClock.h"
#include "NTPClients.h"

// NTPClients::expireClients() reaches for the real global `localClock`
// directly (not injected, unlike ClockDiscipline/ClockHoldover/NTPServer)
// -- this test binary is the sole definition of that global, same as
// teensy-ntp.ino normally provides it.
NTPClock localClock;

static ip4_addr_t addr(uint32_t raw) {
  ip4_addr_t a;
  a.addr = raw;
  return a;
}

void test_findClient_no_match_before_any_addRx() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  TEST_ASSERT_NULL(clients.findClient(&a, WireNtpTime(100), 0));
}

void test_addRx_then_findClient_matches_exactly() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  clients.addRx(&a, 123, WireNtpTime(1000), 5);
  struct client *found = clients.findClient(&a, WireNtpTime(1000), 5);

  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL(1000, found->rx_s.v);
  TEST_ASSERT_EQUAL(5, found->rx_subs);
  // addRx() resets tx_s/tx_subs -- no addTx() has happened yet.
  TEST_ASSERT_EQUAL(0, found->tx_s.v);
}

void test_findClient_rejects_wrong_address() {
  NTPClients clients;
  ip4_addr_t a = addr(1);
  ip4_addr_t other = addr(2);

  clients.addRx(&a, 123, WireNtpTime(1000), 5);

  TEST_ASSERT_NULL(clients.findClient(&other, WireNtpTime(1000), 5));
}

void test_findClient_rejects_wrong_rx_seconds() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  clients.addRx(&a, 123, WireNtpTime(1000), 5);

  TEST_ASSERT_NULL(clients.findClient(&a, WireNtpTime(1001), 5));
}

void test_findClient_rejects_wrong_rx_fractional() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  clients.addRx(&a, 123, WireNtpTime(1000), 5);

  TEST_ASSERT_NULL(clients.findClient(&a, WireNtpTime(1000), 6));
}

// A second addRx() for the same address reuses that address's existing
// slot rather than creating a new entry -- confirmed indirectly: after the
// second call, only the second rx_s is findable, the first no longer is.
void test_addRx_same_address_reuses_slot() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  clients.addRx(&a, 123, WireNtpTime(1000), 0);
  clients.addRx(&a, 123, WireNtpTime(2000), 0);

  TEST_ASSERT_NULL(clients.findClient(&a, WireNtpTime(1000), 0));
  TEST_ASSERT_NOT_NULL(clients.findClient(&a, WireNtpTime(2000), 0));
}

void test_addTx_updates_matching_client() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  clients.addRx(&a, 123, WireNtpTime(1000), 0);
  clients.addTx(&a, 123, WireNtpTime(1500), 9);

  struct client *found = clients.findClient(&a, WireNtpTime(1000), 0);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL(1500, found->tx_s.v);
  TEST_ASSERT_EQUAL(9, found->tx_subs);
}

// addTx() only updates an existing (addr, port) match -- it must not create
// a new client entry, since findClient()'s interleaved-mode lookup depends
// on rx_s having been set by a real addRx() first.
void test_addTx_without_matching_addRx_is_a_noop() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  clients.addTx(&a, 123, WireNtpTime(1500), 9);

  TEST_ASSERT_NULL(clients.findClient(&a, WireNtpTime(1500), 9));
}

// addTx() matches on (address, port) -- a different port for the same
// address must not be updated by it.
void test_addTx_requires_matching_port() {
  NTPClients clients;
  ip4_addr_t a = addr(1);

  clients.addRx(&a, 123, WireNtpTime(1000), 0);
  clients.addTx(&a, 456, WireNtpTime(1500), 9);

  struct client *found = clients.findClient(&a, WireNtpTime(1000), 0);
  TEST_ASSERT_NOT_NULL(found);
  TEST_ASSERT_EQUAL(0, found->tx_s.v);
}

// Generic expiry correctness, well clear of any leap-second-offset edge:
// a client refreshed moments ago survives, one refreshed long before the
// 4096s window does not.
void test_expireClients_generic_fresh_survives_stale_expires() {
  NTPClients clients;
  ip4_addr_t fresh = addr(1), stale = addr(2);

  // 2020ish, offset 37 throughout -- see test-LeapSeconds.cpp's
  // test_tai_to_wire_ntp_normal_second for the same reference value.
  TaiNtpTime taiNow(3800000000UL);
  uint32_t wireNow = 3800000000UL - 37;
  localClock.setTime(0, taiNow);
  When(Method(ArduinoFake(), micros)).Return(0); // no elapsed time since setTime()'s anchor

  clients.addRx(&fresh, 1, WireNtpTime(wireNow - 10), 0);
  clients.addRx(&stale, 2, WireNtpTime(wireNow - 5000), 0);

  clients.expireClients();

  TEST_ASSERT_NOT_NULL(clients.findClient(&fresh, WireNtpTime(wireNow - 10), 0));
  TEST_ASSERT_NULL(clients.findClient(&stale, WireNtpTime(wireNow - 5000), 0));
}

// Regression test for the TAI/wire domain bug fixed alongside the
// WireNtpTime/TaiNtpTime typed wrappers: expireClients() must convert
// localClock's TAI-like "now" to wire format before comparing against the
// wire-domain rx_s it stores, or every client expires up to ~37s early.
// This client is genuinely fresh (10s old, well inside the 4096s window)
// once the domain conversion is applied correctly, but would incorrectly
// register as ~4127s old -- past the window -- if "now" were compared
// without subtracting the 37s leap offset first.
void test_expireClients_uses_wire_domain_not_raw_tai_now() {
  NTPClients clients;
  ip4_addr_t client = addr(1);

  TaiNtpTime taiNow(3800000000UL);
  uint32_t wireNow = 3800000000UL - 37;
  localClock.setTime(0, taiNow);
  When(Method(ArduinoFake(), micros)).Return(0); // no elapsed time since setTime()'s anchor

  // 4090s old in wire-domain terms: inside the 4096s window by only 6s --
  // narrow enough that the ~37s domain gap would flip the outcome if the
  // conversion were missing (see TODO.md, "Test coverage for NTPServer").
  clients.addRx(&client, 1, WireNtpTime(wireNow - 4090), 0);

  clients.expireClients();

  TEST_ASSERT_NOT_NULL(clients.findClient(&client, WireNtpTime(wireNow - 4090), 0));
}

// Regression test for the wraparound bug fixed alongside elapsedWithin():
// expireClients() used to compute `expire_time = nowWire - 4096` and compare
// via raw `rx_s.v < expire_time.v`. Right at the 32-bit NTP wire-seconds
// wrap (~136 years out, same wraparound class as TODO.md/DONE.md's "NTP
// timestamp era rollover (Y2036)" but one domain over), a client that
// registered shortly before the wrap has a large rx_s.v, while `nowWire` is
// a small value just past it -- `expire_time.v` then underflows to a huge
// value, and the genuinely-fresh client's large rx_s.v compares as "less
// than" it, incorrectly expiring it. elapsedWithin()'s wraparound-correct
// subtraction must not have this failure mode.
void test_expireClients_survives_wire_domain_wrap() {
  NTPClients clients;
  ip4_addr_t fresh = addr(1);

  // taiNow=40 is a small TaiNtpTime value -- LeapSeconds.cpp's lookup is
  // itself wraparound-safe now (see TODO.md/DONE.md), so a small value here
  // correctly reads as "shortly after the real 2036-02-07 wrap" (37s offset
  // applies) rather than aliasing to "before 1972" (offset 0). That makes
  // taiToWireNtp(taiNow) come out to a small *wire* value too (3), which is
  // what this test actually needs: wireRx below is chosen to be a handful
  // of wire-seconds before that, i.e. just before the wire-domain's own
  // wrap -- fresh either way, since this test is about
  // NTPClients::expireClients()'s own wraparound-safe elapsedWithin()
  // comparison, not the leap-second conversion.
  uint32_t wireRx = 0xFFFFFFFFUL - 4; // a few wire-seconds before taiNow's wire equivalent
  TaiNtpTime taiNow(40);
  localClock.setTime(0, taiNow);
  When(Method(ArduinoFake(), micros)).Return(0);

  clients.addRx(&fresh, 1, WireNtpTime(wireRx), 0);

  clients.expireClients();

  TEST_ASSERT_NOT_NULL(clients.findClient(&fresh, WireNtpTime(wireRx), 0));
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_findClient_no_match_before_any_addRx);
  RUN_TEST(test_addRx_then_findClient_matches_exactly);
  RUN_TEST(test_findClient_rejects_wrong_address);
  RUN_TEST(test_findClient_rejects_wrong_rx_seconds);
  RUN_TEST(test_findClient_rejects_wrong_rx_fractional);
  RUN_TEST(test_addRx_same_address_reuses_slot);
  RUN_TEST(test_addTx_updates_matching_client);
  RUN_TEST(test_addTx_without_matching_addRx_is_a_noop);
  RUN_TEST(test_addTx_requires_matching_port);
  RUN_TEST(test_expireClients_generic_fresh_survives_stale_expires);
  RUN_TEST(test_expireClients_uses_wire_domain_not_raw_tai_now);
  RUN_TEST(test_expireClients_survives_wire_domain_wrap);
  return UNITY_END();
}
