#pragma once
#include <stddef.h>
#include <stdint.h>
#include "NtpTimestamp.h"
#include "NTPPacket.h"

// Pure, lwIP-independent pieces of NTPServer::recv()'s packet-field
// computation, pulled out so they can be host-tested without needing to
// mock lwIP's pbuf/udp_pcb API. None of these touch pbuf, udp_pcb, or
// ip_addr_t -- NTPServer::recv() itself stays the lwIP-facing shell:
// extract fields from the pbuf, call these, write the results into the
// response packet, call udp_sendto(). See TODO.md, "Test coverage for
// NTPServer".

// Must be checked before reading any other field of the request packet --
// a pbuf smaller than sizeof(ntp_packet) doesn't necessarily have
// version/mode (or anything else) safely readable.
bool ntpRequestLengthIsValid(size_t requestLen);

// Only call once ntpRequestLengthIsValid() has confirmed it's safe to read
// these fields off the request packet.
bool ntpRequestVersionAndModeAreValid(uint8_t version, uint8_t mode);

// The three response fields that together signal sync status and any
// pending leap second: stratum 16 / NTP_LEAP_UNSYNC when unsynced
// (reftime == 0) or dispersion is over the 1-second (0x10000) threshold,
// stratum 1 with the leap indicator looked up from the compiled
// LeapSeconds table otherwise. `ident` is host byte order ("PPS" as
// 0x50505300, or 0 when unsynced) -- the caller applies htonl() when
// writing it into the wire packet, same as every other field here.
struct NTPResponseHeader {
  uint8_t stratum;
  uint32_t ident;
  uint8_t leap;
};
NTPResponseHeader selectNTPResponseHeader(TaiNtpTime reftime, uint32_t dispersion);

// NTP caps the poll interval it echoes back at 2^12 seconds (~68 minutes).
uint8_t clampNTPPoll(uint8_t requestedPoll);

struct NTPWireTimestamp {
  WireNtpTime seconds;
  uint32_t fractional;
};

// Converts a TAI-like clock sample (as returned by NTPClock::getTime()),
// plus a small fixed sub-second latency correction (see the
// NTP_*_LATENCY_CORRECTION constants below), into the real NTP wire-format
// timestamp to put on the wire.
NTPWireTimestamp ntpWireTimestampFromTai(TaiNtpTime taiSeconds, uint32_t taiFractional, int64_t latencyCorrectionFracUnits);

// Same, but for a timestamp that's already wire format -- interleaved-mode
// responses reuse a previously-stored, already-converted transmit
// timestamp (see NTPClients::addTx()/NTPServer::addTxTimestamp()) and only
// need the latency correction applied, not another domain conversion.
NTPWireTimestamp ntpWireTimestampFromWire(WireNtpTime wireSeconds, uint32_t wireFractional, int64_t latencyCorrectionFracUnits);

// The three latency-correction constants recv() uses, exposed so both the
// production call site and tests use the same values instead of
// duplicating the datasheet-derived numbers. All in 2^32 fractional-second
// units (see NTPResponseFields.cpp for where each one comes from).
extern const int64_t NTP_RX_LATENCY_CORRECTION;
extern const int64_t NTP_TX_BASIC_LATENCY_CORRECTION;
extern const int64_t NTP_TX_INTERLEAVED_LATENCY_CORRECTION;
