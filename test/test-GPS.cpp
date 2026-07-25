#include "test.h"
#include "DateTime.h"
#include "GPS.h"
#include "InputCapture.h"

#define MOCK_MILLIS 1000000

// Mock InputCapture
InputCapture pps;

void InputCapture::newCapture(uint32_t count) {
  captures = captures + 1;
  lastCount = count;
  lastMillis = 1000;
}

InputCapture::InputCapture() {
  lastCount = 0;
  lastMillis = 0;
  captures = 0;
}

void InputCapture::begin() {
}

// asserts decode() accepts the message's checksum, i.e. it breaks out of
// the byte-feeding loop right at the trailing \r instead of running past it
static void assert_checksum_accepted(const char *mockMessage) {
  GPSDateTime gps(&Serial);
  uint32_t i;

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(i = 0; i < strlen(mockMessage); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage[i]);
    if(gps.decode()) {
      break;
    }
  }
  TEST_ASSERT_EQUAL(strlen(mockMessage)-2, i);
}

void test_checksum() {
  // checksums in 0x0..0xF used to fail: parity_ was compared against the
  // message's 2-char zero-padded hex field via a non-zero-padded
  // String(parity_, HEX), so e.g. checksum 0x01 rendered as "1" and never
  // matched the message's "01". Cover both a zero and a non-zero
  // single-hex-digit checksum.
  assert_checksum_accepted("$GNZDA,031700.000,17,12,p019,00,00*00\r\n"); // checksum 0x00
  assert_checksum_accepted("$GNZDA,031700.000,17,12,p018,00,00*01\r\n"); // checksum 0x01
}

// helper: simulate a real PPS pulse arriving (the InputCapture ISR firing),
// establishing a new (count, millis=1000) reference for this fix cycle.
// Must be called once per cycle, not once per sentence -- multiple NMEA
// sentences share a single pulse on real hardware.
static void firePulse(uint32_t ppsCount) {
  pps.newCapture(ppsCount);
}

// helper: feed every byte of a mocked serial message through decode(), with
// millis() mocked to mockMillis for the duration -- as if this sentence
// arrived while that was the current local time.
static bool feed(GPSDateTime &gps, const char *mockMessage, uint32_t mockMillis) {
  When(Method(ArduinoFake(), millis)).Return(mockMillis);
  bool updated = false;
  for(uint32_t i = 0; i < strlen(mockMessage); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage[i]);
    if(gps.decode()) {
      updated = true;
    }
  }
  return updated;
}

// RMC alone (no GGA) is sufficient to capture a PPS reference and decode a
// full date/time -- GPS_USES_RMC used to have to be defined at compile
// time for this to parse at all; sentence-type handling is now runtime, so
// any module emitting RMC works without a special build.
void test_rmc_alone() {
  GPSDateTime gps(&Serial);
  firePulse(4242);
  bool updated = feed(gps, "$GPRMC,131102.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,220307,0.0,E,A*25\r\n", 8000);

  TEST_ASSERT_TRUE(updated);
  TEST_ASSERT_EQUAL(8000, gps.capturedAt());
  TEST_ASSERT_EQUAL(4242, gps.ppsCounter());

  DateTime decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(2007, decoded.year());
  TEST_ASSERT_EQUAL(3, decoded.month());
  TEST_ASSERT_EQUAL(22, decoded.day());
  TEST_ASSERT_EQUAL(13, decoded.hour());
  TEST_ASSERT_EQUAL(11, decoded.minute());
  TEST_ASSERT_EQUAL(2, decoded.second());
}

// GGA arriving before RMC in a fix cycle should be the one that captures
// the PPS reference (it's the earlier message, closer to the pulse edge),
// even though RMC is what actually supplies the date/time. Regression
// coverage for issue #6 ("code assumes RMC comes after GGA"), now that
// sentence order is auto-detected at runtime instead of picked via
// GPS_USES_RMC/GPS_GGA_IS_FIRST at compile time -- and, unlike the old
// compile-time flags, this path is exercised by the default test build
// instead of only being checked with a standalone `g++ -c`.
void test_gga_then_rmc_captures_at_gga() {
  GPSDateTime gps(&Serial);

  // one pulse, three sentences -- GSA, then GGA, then RMC, as a real
  // receiver's burst would look. The unrelated GSA ahead of GGA must not
  // itself trigger, or block, the capture.
  firePulse(1111);
  feed(gps, "$GPGSA,A,3,04,07,09,03,08,22,16,27,,,,,1.4,0.8,1.2*3F\r\n", 4000);
  feed(gps, "$GPGGA,144326.00,5107.0017737,N,11402.3291611,W,1,08,0.9,545.4,M,46.9,M,,*7D\r\n", 5000);
  bool updated = feed(gps, "$GPRMC,144326.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,210307,0.0,E,A*20\r\n", 9000);

  TEST_ASSERT_TRUE(updated);
  TEST_ASSERT_EQUAL(5000, gps.capturedAt());  // GGA's arrival time, not RMC's
  TEST_ASSERT_EQUAL(1111, gps.ppsCounter());  // this cycle's pulse, captured once

  DateTime decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(2007, decoded.year());
  TEST_ASSERT_EQUAL(3, decoded.month());
  TEST_ASSERT_EQUAL(21, decoded.day());
}

// The reverse order: RMC arrives first, so RMC captures the PPS reference
// and GGA arriving afterward (same pulse) must not overwrite it.
void test_rmc_then_gga_captures_at_rmc() {
  GPSDateTime gps(&Serial);

  firePulse(2222);
  bool updated = feed(gps, "$GPRMC,144326.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,210307,0.0,E,A*20\r\n", 9000);
  feed(gps, "$GPGGA,144326.00,5107.0017737,N,11402.3291611,W,1,08,0.9,545.4,M,46.9,M,,*7D\r\n", 5000);

  TEST_ASSERT_TRUE(updated);
  TEST_ASSERT_EQUAL(9000, gps.capturedAt());  // RMC's arrival time, not GGA's
  TEST_ASSERT_EQUAL(2222, gps.ppsCounter());
}

// The capture must re-arm on the next PPS pulse -- otherwise every cycle
// after the first GGA+RMC pair would keep reusing that first cycle's stale
// capture instead of taking a fresh one on the next pulse.
void test_capture_resets_on_next_pulse() {
  GPSDateTime gps(&Serial);

  firePulse(1111);
  feed(gps, "$GPGGA,144326.00,5107.0017737,N,11402.3291611,W,1,08,0.9,545.4,M,46.9,M,,*7D\r\n", 5000);
  feed(gps, "$GPRMC,144326.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,210307,0.0,E,A*20\r\n", 9000);
  TEST_ASSERT_EQUAL(5000, gps.capturedAt());

  // next pulse -- second cycle, RMC only, distinct values -- must not
  // still be showing the first cycle's GGA capture.
  firePulse(4242);
  feed(gps, "$GPRMC,131102.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,220307,0.0,E,A*25\r\n", 12000);
  TEST_ASSERT_EQUAL(12000, gps.capturedAt());
  TEST_ASSERT_EQUAL(4242, gps.ppsCounter());
}

// A module could plausibly emit both ZDA and RMC in the same fix cycle
// (unlike GGA, both are independently sufficient to supply a full
// date/time). Only the first to complete should commit -- the second must
// not re-commit, and decode() must not report a second update, for what's
// really the same PPS pulse. Without this guard the caller would double-
// feed one pulse into the PID sample pipeline as if it were two.
void test_zda_then_rmc_only_first_commits() {
  GPSDateTime gps(&Serial);

  firePulse(7777);
  bool zdaUpdated = feed(gps, "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n", 5000);
  bool rmcUpdated = feed(gps, "$GPRMC,144326.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,210307,0.0,E,A*20\r\n", 9000);

  TEST_ASSERT_TRUE(zdaUpdated);
  TEST_ASSERT_FALSE(rmcUpdated);
  TEST_ASSERT_EQUAL(5000, gps.capturedAt());  // ZDA's arrival time, not RMC's

  DateTime decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(2019, decoded.year());
  TEST_ASSERT_EQUAL(12, decoded.month());
  TEST_ASSERT_EQUAL(17, decoded.day());
}

// Same as above with the order reversed: RMC first commits, ZDA arriving
// afterward in the same cycle must not override it.
void test_rmc_then_zda_only_first_commits() {
  GPSDateTime gps(&Serial);

  firePulse(8888);
  bool rmcUpdated = feed(gps, "$GPRMC,144326.00,A,5107.0017737,N,11402.3291611,W,0.080,323.3,210307,0.0,E,A*20\r\n", 9000);
  bool zdaUpdated = feed(gps, "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n", 5000);

  TEST_ASSERT_TRUE(rmcUpdated);
  TEST_ASSERT_FALSE(zdaUpdated);
  TEST_ASSERT_EQUAL(9000, gps.capturedAt());  // RMC's arrival time, not ZDA's

  DateTime decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(2007, decoded.year());
  TEST_ASSERT_EQUAL(3, decoded.month());
  TEST_ASSERT_EQUAL(21, decoded.day());
}

// Once PPS is lost, decode()'s commit() gate (committedThisPulse_) never
// resets (it only clears on a fresh PPS pulse -- see GPS.cpp), so decode()
// keeps returning false and GPSnow() stays frozen at the last committed
// fix forever, even though the module may well still be sending valid
// NMEA. reportedUpdate()/reportedNow() track those sentences independent
// of that gate, so callers (the web UI's "GPS reported time") don't freeze
// too. Found via bench testing with PPS physically disconnected.
void test_reported_update_survives_pps_loss() {
  GPSDateTime gps(&Serial);
  const char mockMessage[]  = "$GPZDA,031659.000,17,12,2019,00,00*51\r\n";
  const char mockMessage2[] = "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n";

  firePulse(1);
  bool committed = feed(gps, mockMessage, 5000);
  TEST_ASSERT_TRUE(committed);
  TEST_ASSERT_TRUE(gps.reportedUpdate()); // consumes it

  // no firePulse() here -- simulates PPS having stopped entirely.
  committed = feed(gps, mockMessage2, 9000);
  TEST_ASSERT_FALSE(committed); // commit() suppressed, same as before this fix
  TEST_ASSERT_TRUE(gps.reportedUpdate()); // but the raw sentence was still seen

  DateTime committedTime = gps.GPSnow();
  TEST_ASSERT_EQUAL(59, committedTime.second()); // still the first sentence's :59

  DateTime reported = gps.reportedNow();
  TEST_ASSERT_EQUAL(2019, reported.year());
  TEST_ASSERT_EQUAL(12, reported.month());
  TEST_ASSERT_EQUAL(17, reported.day());
  TEST_ASSERT_EQUAL(3, reported.hour());
  TEST_ASSERT_EQUAL(17, reported.minute());
  TEST_ASSERT_EQUAL(0, reported.second()); // second sentence's :00, not frozen
}

// reportedUpdate() is consume-on-read: a caller that doesn't check it after
// every decode() call must not see a stale true left over from an earlier
// sentence.
void test_reported_update_is_consumed_on_read() {
  GPSDateTime gps(&Serial);
  const char mockMessage[] = "$GPZDA,031659.000,17,12,2019,00,00*51\r\n";

  firePulse(1);
  feed(gps, mockMessage, 5000);
  TEST_ASSERT_TRUE(gps.reportedUpdate());
  TEST_ASSERT_FALSE(gps.reportedUpdate());
}

// Satellite counts (strongSignal/weakSignal/noSignal) used to only publish
// and reset inside commit(), gated by committedThisPulse_ -- which, exactly
// like reportedUpdate() above, gets stuck once PPS is lost. Unlike the
// date/time fields (which just freeze), decodeGSV()'s
// strongSignalNext/weakSignalNext/noSignalNext *accumulate* per satellite
// entry with nothing left to reset them, so a long holdover built up an
// unbounded total instead of freezing -- observed on the bench as
// weakSignals reported as 1436 after a ~5 minute holdover (data/run4/notes).
// Feeding the same GSV+ZDA cycle twice with only one PPS pulse (simulating
// PPS loss between the two) must show the second cycle's own count, not the
// two cycles summed.
void test_satellite_counts_reset_each_cycle_during_pps_loss() {
  GPSDateTime gps(&Serial);
  const char *gsv1 = "$GPGSV,3,1,10,04,82,026,36,16,54,040,35,09,46,320,20,27,38,117,37*74\r\n";
  const char *gsv2 = "$GPGSV,3,2,10,03,32,209,31,08,25,161,33,07,24,288,24,26,23,043,23*7D\r\n";
  const char *gsv3 = "$GPGSV,3,3,10,22,17,190,30,31,00,081,*7E\r\n";
  const char *zda  = "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n";

  firePulse(1);
  feed(gps, gsv1, 5000);
  feed(gps, gsv2, 5000);
  feed(gps, gsv3, 5000);
  feed(gps, zda, 5000);

  TEST_ASSERT_EQUAL(6, gps.strongSignals());
  TEST_ASSERT_EQUAL(3, gps.weakSignals());
  TEST_ASSERT_EQUAL(1, gps.noSignals());

  // no firePulse() here -- PPS stays lost for this second cycle, same as a
  // real holdover episode.
  feed(gps, gsv1, 9000);
  feed(gps, gsv2, 9000);
  feed(gps, gsv3, 9000);
  feed(gps, zda, 9000);

  TEST_ASSERT_EQUAL(6, gps.strongSignals()); // this cycle's count, not 12
  TEST_ASSERT_EQUAL(3, gps.weakSignals());   // this cycle's count, not 6
  TEST_ASSERT_EQUAL(1, gps.noSignals());     // this cycle's count, not 2
}

void test_decode() {
  const char mockMessage[]  = "$GPZDA,031659.000,17,12,2019,00,00*51\r\n";
  const char mockMessage2[] = "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n";
  const char mockMessage3[] = "$GNZDA,031700.000,17,12,2019,00,00*42\n";
  GPSDateTime gps(&Serial);

  // each mockMessage below stands in for a distinct second's fix, which on
  // real hardware always has its own PPS pulse ahead of it.
  pps.newCapture(1);
  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage[i]);
    if(gps.decode()) {
      // marked at \r
      TEST_ASSERT_EQUAL(strlen(mockMessage)-2, i);
    }
  }

  DateTime decoded = gps.GPSnow();
  // +37: unixtime() is TAI-like (leap-second-adjusted, see DateTime.h) --
  // the cumulative offset in effect since 2017-01-01 is 37s.
  TEST_ASSERT_EQUAL(1576552619 + 37, decoded.unixtime());

  pps.newCapture(2);
  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage2); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage2[i]);
    if(gps.decode()) {
      // marked at \r
      TEST_ASSERT_EQUAL(strlen(mockMessage2)-2, i);
    }
  }

  decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552620 + 37, decoded.unixtime());

  pps.newCapture(3);
  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage3); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage3[i]);
    if(gps.decode()) {
      // marked at \n
      TEST_ASSERT_EQUAL(strlen(mockMessage3)-1, i);
    }
  }

  decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552620 + 37, decoded.unixtime());
  TEST_ASSERT_EQUAL(MOCK_MILLIS, gps.capturedAt());
}

void test_satellites() {
  const char mockMessage1[] = R"GPS(
$GPGSA,A,3,04,07,09,03,08,22,16,27,,,,,1.4,0.8,1.2*3F
$GLGSA,A,3,65,76,74,,,,,,,,,,1.4,0.8,1.2*21
$GPGSV,3,1,10,04,82,026,36,16,54,040,35,09,46,320,20,27,38,117,37*74
$GPGSV,3,2,10,03,32,209,31,08,25,161,33,07,24,288,24,26,23,043,23*7D
$GPGSV,3,3,10,22,17,190,30,31,00,081,*7E
$GLGSV,2,1,08,65,68,199,27,76,45,282,32,66,35,326,,72,27,165,*6D
$GLGSV,2,2,08,85,09,100,,74,05,047,19,84,05,047,,77,05,247,*6B
$GPZDA,031700.000,17,12,2019,00,00*5C
)GPS";
  const char mockMessage2[] = R"GPS(
$GNGSA,A,3,03,08,26,09,27,04,07,16,,,,,1.40,0.72,1.20*18
$GNGSA,A,3,66,76,65,85,,,,,,,,,1.40,0.72,1.20*10
$GNGSA,A,3,319,307,321,301,304,327,,,,,,,1.40,0.72,1.20*13
$GPGSV,3,1,12,03,27,205,22,04,84,081,31,07,27,293,18,08,30,158,25*79
$GPGSV,3,2,12,09,52,319,23,16,48,039,28,22,12,187,,26,18,044,22*74
$GPGSV,3,3,12,27,41,110,26,30,00,276,,46,41,230,,51,53,198,*7A
$GLGSV,3,1,11,65,60,195,21,66,41,321,27,72,21,166,,74,01,052,*6E
$GLGSV,3,2,11,75,42,016,25,76,47,292,27,77,08,253,,84,05,041,*6E
$GLGSV,3,3,11,85,13,094,27,86,01,139,08,92,41,297,*5C
$GPZDA,031700.000,17,12,2019,00,00*5C
)GPS";
  GPSDateTime gps(&Serial);

  struct satellite expected1[] = {
    {.id=4, .azimuth=26, .elevation=82, .snr=36},
    {.id=16, .azimuth=40, .elevation=54, .snr=35},
    {.id=9, .azimuth=320, .elevation=46, .snr=20},
    {.id=27, .azimuth=117, .elevation=38, .snr=37},
    {.id=3, .azimuth=209, .elevation=32, .snr=31},
    {.id=8, .azimuth=161, .elevation=25, .snr=33},
    {.id=7, .azimuth=288, .elevation=24, .snr=24},
    {.id=26, .azimuth=43, .elevation=23, .snr=23},
    {.id=22, .azimuth=190, .elevation=17, .snr=30},
    {.id=31, .azimuth=81, .elevation=0, .snr=0},
    {.id=65, .azimuth=199, .elevation=68, .snr=27},
    {.id=76, .azimuth=282, .elevation=45, .snr=32},
    {.id=66, .azimuth=326, .elevation=35, .snr=0},
    {.id=72, .azimuth=165, .elevation=27, .snr=0},
    {.id=85, .azimuth=100, .elevation=9, .snr=0},
    {.id=74, .azimuth=47, .elevation=5, .snr=19},
    {.id=84, .azimuth=47, .elevation=5, .snr=0},
    {.id=77, .azimuth=247, .elevation=5, .snr=0},
    {0, 0, 0, 0}
  };
  struct satellite expected2[] = {
    {.id=3, .azimuth=205, .elevation=27, .snr=22},
    {.id=4, .azimuth=81, .elevation=84, .snr=31},
    {.id=7, .azimuth=293, .elevation=27, .snr=18},
    {.id=8, .azimuth=158, .elevation=30, .snr=25},
    {.id=9, .azimuth=319, .elevation=52, .snr=23},
    {.id=16, .azimuth=39, .elevation=48, .snr=28},
    {.id=22, .azimuth=187, .elevation=12, .snr=0},
    {.id=26, .azimuth=44, .elevation=18, .snr=22},
    {.id=27, .azimuth=110, .elevation=41, .snr=26},
    {.id=30, .azimuth=276, .elevation=0, .snr=0},
    {.id=46, .azimuth=230, .elevation=41, .snr=0},
    {.id=51, .azimuth=198, .elevation=53, .snr=0},
    {.id=65, .azimuth=195, .elevation=60, .snr=21},
    {.id=66, .azimuth=321, .elevation=41, .snr=27},
    {.id=72, .azimuth=166, .elevation=21, .snr=0},
    {.id=74, .azimuth=52, .elevation=1, .snr=0},
    {.id=75, .azimuth=16, .elevation=42, .snr=25},
    {.id=76, .azimuth=292, .elevation=47, .snr=27},
    {.id=77, .azimuth=253, .elevation=8, .snr=0},
    {.id=84, .azimuth=41, .elevation=5, .snr=0},
    {.id=85, .azimuth=94, .elevation=13, .snr=27},
    {.id=86, .azimuth=139, .elevation=1, .snr=8},
    {.id=92, .azimuth=297, .elevation=41, .snr=0},
    {0, 0, 0, 0}
  };

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  uint32_t timesTrue = 0;
  for(uint32_t i = 0; i < strlen(mockMessage1); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage1[i]);
    if(gps.decode()) {
      timesTrue++;
    }
  }
  TEST_ASSERT_EQUAL(1, timesTrue);
  TEST_ASSERT_EQUAL(3, gps.lockStatus());
  TEST_ASSERT_EQUAL(8, gps.strongSignals());
  TEST_ASSERT_EQUAL(4, gps.weakSignals());
  TEST_ASSERT_EQUAL(6, gps.noSignals());
  const struct satellite *l = gps.getSatellites();
  for(uint8_t i = 0; expected1[i].id != 0 && i < MAX_SATELLITES; i++) {
    TEST_ASSERT_EQUAL(expected1[i].id, l[i].id);
    TEST_ASSERT_EQUAL(expected1[i].azimuth, l[i].azimuth);
    TEST_ASSERT_EQUAL(expected1[i].elevation, l[i].elevation);
    TEST_ASSERT_EQUAL(expected1[i].snr, l[i].snr);
  }
  TEST_ASSERT_EQUAL(1.4, gps.getPdop());
  TEST_ASSERT_EQUAL(0.8, gps.getHdop());
  TEST_ASSERT_EQUAL(1.2, gps.getVdop());

  // a distinct second's fix -- its own PPS pulse ahead of it, same as in
  // test_decode.
  pps.newCapture(101);
  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  timesTrue = 0;
  for(uint32_t i = 0; i < strlen(mockMessage2); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage2[i]);
    if(gps.decode()) {
      timesTrue++;
    }
  }
  TEST_ASSERT_EQUAL(1, timesTrue);
  TEST_ASSERT_EQUAL(3, gps.lockStatus());
  TEST_ASSERT_EQUAL(8, gps.strongSignals());
  TEST_ASSERT_EQUAL(5, gps.weakSignals());
  TEST_ASSERT_EQUAL(10, gps.noSignals());
  l = gps.getSatellites();
  for(uint8_t i = 0; expected2[i].id != 0 && i < MAX_SATELLITES; i++) {
    TEST_ASSERT_EQUAL(expected2[i].id, l[i].id);
    TEST_ASSERT_EQUAL(expected2[i].azimuth, l[i].azimuth);
    TEST_ASSERT_EQUAL(expected2[i].elevation, l[i].elevation);
    TEST_ASSERT_EQUAL(expected2[i].snr, l[i].snr);
  }
  TEST_ASSERT_EQUAL(1.4, gps.getPdop());
  TEST_ASSERT_EQUAL(0.72, gps.getHdop());
  TEST_ASSERT_EQUAL(1.2, gps.getVdop());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_decode);
  RUN_TEST(test_satellites);
  RUN_TEST(test_checksum);
  RUN_TEST(test_rmc_alone);
  RUN_TEST(test_gga_then_rmc_captures_at_gga);
  RUN_TEST(test_rmc_then_gga_captures_at_rmc);
  RUN_TEST(test_capture_resets_on_next_pulse);
  RUN_TEST(test_zda_then_rmc_only_first_commits);
  RUN_TEST(test_rmc_then_zda_only_first_commits);
  RUN_TEST(test_reported_update_survives_pps_loss);
  RUN_TEST(test_reported_update_is_consumed_on_read);
  RUN_TEST(test_satellite_counts_reset_each_cycle_during_pps_loss);
  return UNITY_END();
}
