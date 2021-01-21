#include "test.h"
#include "DateTime.h"
#include "GPS.h"
#include "InputCapture.h"

#define MOCK_MILLIS 1000000

// Mock InputCapture
InputCapture pps;

void InputCapture::newCapture(uint32_t count) {
  captures++;
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

void test_decode() {
  const char mockMessage[]  = "$GPZDA,031659.000,17,12,2019,00,00*51\r\n";
  const char mockMessage2[] = "$GPZDA,031700.000,17,12,2019,00,00*5C\r\n";
  const char mockMessage3[] = "$GNZDA,031700.000,17,12,2019,00,00*42\n";
  GPSDateTime gps(&Serial);

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage[i]);
    if(gps.decode()) {
      // marked at \r
      TEST_ASSERT_EQUAL(strlen(mockMessage)-2, i);
    }
  }

  DateTime decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552619, decoded.unixtime());

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage2); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage2[i]);
    if(gps.decode()) {
      // marked at \r
      TEST_ASSERT_EQUAL(strlen(mockMessage2)-2, i);
    }
  }

  decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552620, decoded.unixtime());

  When(Method(ArduinoFake(), millis)).Return(MOCK_MILLIS);

  for(uint32_t i = 0; i < strlen(mockMessage3); i++) {
    When(Method(ArduinoFake(Serial), read)).Return(mockMessage3[i]);
    if(gps.decode()) {
      // marked at \n
      TEST_ASSERT_EQUAL(strlen(mockMessage3)-1, i);
    }
  }

  decoded = gps.GPSnow();
  TEST_ASSERT_EQUAL(1576552620, decoded.unixtime());
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
  return UNITY_END();
}
