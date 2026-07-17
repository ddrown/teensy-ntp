#include "NTPResponseFields.h"
#include "LeapSeconds.h"

// the values below are in 2^32 fractional second units
// adjusting from preamble timestamp to trailer timestamp: 752 bits at 100M
#define RX_TRAILER 32298
// estimate of delay between TX software timestamp and udp_sendto sending packet, for non-interleaved clients
#define TX_DELAY 16492
// From DP83825I datasheet, page 10
// "Slave RMII Rising edge XI clock with assertion TX_EN to SSD symbol on MDI (100M)"
// 105 ns
#define TX_PHY 451
// "SSD symbol on MDI to Slave RMII Rising edge of XI clock with assertion of CRS_DV (100M)"
// 350ns
#define RX_PHY 1503

const int64_t NTP_RX_LATENCY_CORRECTION = RX_TRAILER - RX_PHY;
const int64_t NTP_TX_BASIC_LATENCY_CORRECTION = TX_DELAY + TX_PHY;
const int64_t NTP_TX_INTERLEAVED_LATENCY_CORRECTION = TX_PHY;

bool ntpRequestLengthIsValid(size_t requestLen) {
  return requestLen >= sizeof(struct ntp_packet);
}

bool ntpRequestVersionAndModeAreValid(uint8_t version, uint8_t mode) {
  if (version < 2 || version > 4) {
    return false;
  }
  return mode == NTP_MODE_CLIENT;
}

NTPResponseHeader selectNTPResponseHeader(TaiNtpTime reftime, uint32_t dispersion) {
  NTPResponseHeader header;

  if (reftime.v == 0 || dispersion > 0x10000) {
    // no sync or dispersion over 1s
    header.stratum = 16;
    header.ident = 0;
    header.leap = NTP_LEAP_UNSYNC;
    return header;
  }

  header.stratum = 1;
  header.ident = 0x50505300; // "PPS"

  LeapSecondType pendingType;
  if (leapSecondPendingToday(taiToWireNtp(reftime), &pendingType)) {
    header.leap = (pendingType == LEAP_DELETE) ? NTP_LEAP_59S : NTP_LEAP_61S;
  } else {
    header.leap = NTP_LEAP_NONE;
  }
  return header;
}

uint8_t clampNTPPoll(uint8_t requestedPoll) {
  return (requestedPoll > 12) ? 12 : requestedPoll;
}

// Ntp64 is domain-agnostic (see NtpTimestamp.h) -- this stays in the same
// raw-uint32_t domain the caller passed in, and it's up to
// ntpWireTimestampFromTai()/ntpWireTimestampFromWire() below to wrap (and,
// for the TAI case, convert) the result into the right type. Mislabeling
// this intermediate value as already-wire-format would be exactly the kind
// of domain mixup WireNtpTime/TaiNtpTime exist to prevent.
struct RawTimestamp {
  uint32_t seconds;
  uint32_t fractional;
};

static RawTimestamp applyLatencyCorrection(uint32_t seconds, uint32_t fractional, int64_t latencyCorrectionFracUnits) {
  Ntp64 t;
  t.setSeconds(seconds);
  t.setFractional(fractional);
  t.whole += latencyCorrectionFracUnits;

  RawTimestamp result;
  result.seconds = t.seconds();
  result.fractional = t.fractional();
  return result;
}

NTPWireTimestamp ntpWireTimestampFromTai(TaiNtpTime taiSeconds, uint32_t taiFractional, int64_t latencyCorrectionFracUnits) {
  RawTimestamp corrected = applyLatencyCorrection(taiSeconds.v, taiFractional, latencyCorrectionFracUnits);
  // The correction only shifts the sub-second fractional part in practice
  // (a few hundred to a few thousand out of 2^32), so it can't itself have
  // crossed a leap-second boundary; the domain conversion below still uses
  // the (possibly carried-into) whole-seconds part, same as the original
  // inline code did.
  NTPWireTimestamp result;
  result.seconds = taiToWireNtp(TaiNtpTime(corrected.seconds));
  result.fractional = corrected.fractional;
  return result;
}

NTPWireTimestamp ntpWireTimestampFromWire(WireNtpTime wireSeconds, uint32_t wireFractional, int64_t latencyCorrectionFracUnits) {
  RawTimestamp corrected = applyLatencyCorrection(wireSeconds.v, wireFractional, latencyCorrectionFracUnits);
  NTPWireTimestamp result;
  result.seconds = WireNtpTime(corrected.seconds);
  result.fractional = corrected.fractional;
  return result;
}
