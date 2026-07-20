# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.

## README

Currently a bare hardware/software bullet list with no framing. Discussed 2026-07-20;
roughly in priority order.

- [ ] Add an intro paragraph: what this is, and why a Teensy 4.1 with a hardware 1588 timer
      instead of the much more common Raspberry Pi + gpsd + chrony approach (dedicated
      hardware timer vs. Linux scheduling jitter, cost, power, no SD card to corrupt).
- [ ] State what accuracy/precision to actually expect in practice, not just the
      theoretical NTP `precision` field (-24, i.e. 40ns/2^-24s from the 25MHz 1588 counter).
- [ ] Document what happens when GPS/PPS is lost -- the holdover behavior (D-only
      discipline, growing dispersion estimate, eventual stratum-16 fallback) rather than
      leaving readers to assume it just stops serving time.
- [ ] Document `settings.h` options (`GPS_BAUD`, `GPS_SERIAL`, `DHCP_HOSTNAME`,
      `GPS_USES_RMC`) -- currently only mentioned in passing for the RMC case.
- [ ] Add real build/flash steps (Arduino IDE board selection, Teensyduino) -- currently
      only "put this library in your Arduino libraries folder".
- [ ] Add a wiring diagram or pin table beyond the single PPS-pin mention (GPS TX/RX,
      power, ethernet).
- [ ] Add a troubleshooting section: no PPS detected, GPS checksum errors, GPS won't lock.
- [ ] Link out to `TODO.md`/`DONE.md` (project status) and `TESTPLAN.md` (how it's
      verified) instead of leaving them undiscoverable.
- [ ] Call out other differentiators worth a mention: Theil-Sen regression + chi-squared
      drift estimation instead of a naive PI loop, compiled-in leap-second table with a
      TAI-like internal time domain, host-side unit test coverage (ArduinoFake/Unity) for
      an Arduino sketch, NTP interleaved mode, IPv6 support.
- [ ] No `LICENSE` file exists -- confirm with the repo owner whether one should be added
      before treating this as an open question answered by the README.
