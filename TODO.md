# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.
- [ ] `settings.h`'s hardcoded `GPS_BAUD` (`teensy-ntp.ino:104`, `GPS_SERIAL.begin(GPS_BAUD)`) has
      the same "one firmware image per device" problem `DHCP_HOSTNAME` used to (see DONE.md,
      "look up per-device hostname by MAC address instead of settings.h edits") -- this session's
      own baud-mismatch debugging (a second bench unit's GPS module was still at its factory 9600
      while this firmware assumed 115200, and the mismatch produced no error, just silently zero
      satellites/no lock) is exactly the failure mode auto-detection would catch immediately
      instead of by hand. Try a short list of candidate bauds at startup (9600 and 115200 to
      start, structured as an array in `settings.h` the way `hostnameTable[]` already is, so it's
      easy to extend) -- for each, `GPS_SERIAL.begin(candidate)`, listen for a short window, and
      check for at least one sentence with a valid NMEA checksum (reuse `GPS.cpp`'s existing
      checksum verification rather than duplicating it). Stick with the first baud that produces
      a valid sentence; print the detected baud over serial (same spirit as the MAC-address print
      added for hostname lookup) so it's visible without opening `settings.h`.

## Holdover bench session findings (2026-07-22)

From a real PPS-disconnect holdover test (see `holdover.txt`), roughly in priority order.

- [ ] `ClockPID`'s sample buffer is never cleared across a holdover episode --
      `ClockPID_c::reset_clock()` exists (`ClockPID.h:46`) but is dead code, never called anywhere.
      After a long enough GPS/PPS outage, `calculate_d()`'s `remoteDuration * COUNTSPERSECOND`
      (`ClockPID.cpp:97`, `COUNTSPERSECOND` = 25,000,000 on real hardware) mixes a stale
      pre-holdover sample with fresh post-holdover ones spanning a `remoteDuration` large enough to
      overflow the 32-bit multiply, producing a garbage `rawOffsets[]` entry. `chisq()` then blows
      up to a value that overflows `float`, printed literally as `ovf` by Arduino's `Print` class,
      for the ~4 samples it takes to cycle the stale entry out of the 16-entry window. Observed:
      `ppb`/`pidD` stayed sane through the same window (Theil-Sen's median-of-pairwise-slopes is
      robust to one corrupted point), so this looks diagnostics-only, not an actual bad clock
      step -- but worth confirming with a wider range of outage durations, and wiring
      `reset_clock()` into holdover recovery is the likely fix either way.
- [ ] `WebContent`'s `gpstime` fallback to `localClock`'s live time (see DONE.md, "fall back to
      localClock's live time for gpstime before first GPS fix") only covers the window before the
      *first-ever* fix -- `haveGpsTime` latches true permanently once set. During a holdover
      episode (which happens after a real fix), `gpstime` freezes at the last real GPS sample
      instead of falling back live, so the web UI graphs freeze during holdover the same way they
      used to freeze at the NTP epoch before that fix.
- [ ] GPS satellite strong/weak signal counts sometimes jump abruptly (e.g. 5→39) -- observed
      during the 2026-07-22 holdover bench session, not yet investigated.
- [ ] Web UI: "Holdover elapsed" could show elapsed time since the last real PPS pulse instead (or
      in addition), so it lines up directly against "Reference time" for a reader comparing the two.
- [ ] Web UI: the flat `<p>` list of stats at the bottom of the status page is hard to scan --
      consider reorganizing into a table.

## NMEA MITM tooling (for TESTPLAN.md sections 6/7)

Discussed 2026-07-22, revised same day: a USB-UART adapter only gives one serial connection, so a
live two-port relay between the real GPS module and the Teensy isn't practical. Instead: leave the
GPS module's PPS output wired to the Teensy as normal, but disconnect its NMEA TX line and drive
the Teensy's `GPS_SERIAL` input directly from this machine over a single USB-UART, sending
entirely pre-generated, fully fabricated NMEA sentences. Since real GPS PPS edges land almost
exactly on the UTC second boundary, timing sends off this machine's own (NTP-synced) wall clock is
plenty accurate for the 950ms lag tolerance -- no need to observe the real PPS signal directly.
Chosen over a second-MCU relay (Teensy/STM32/ESP32 with two UARTs) for the same reason as before:
the actual hard part is the scenario logic (date arithmetic, multi-fix sequencing, rebasing a
feed), which is much faster to write and debug in Python than as embedded C with a reflash cycle
between iterations -- and since every sentence is fabricated from the start now, there's no
separate "passthrough vs. inject" mode to build either.

- [ ] Generator: one text file per test scenario (6a/6b/6c/7a below), each containing groups of
      full NMEA sentences (one group per intended per-second fix cycle) separated by a marker
      line. Sentences need correct checksums (XOR of all bytes between `$` and `*`) computed at
      generation time, since there's no live passthrough to inherit them from -- an edited
      sentence with a stale checksum is silently dropped before decode (see `GPS.cpp`). Detect/
      match whichever sentence type this GPS module actually emits (ZDA vs. RMC+GGA); `GPS.cpp`'s
      decoding is runtime-detected, so the fixture should generate whichever is actually in use.
- [ ] Player: a `pyserial` program that, given a millisecond offset-from-the-top-of-the-second,
      loops: wait until wall-clock reaches the next second boundary plus that offset, send the
      next group from the file (up to the next marker), repeat until the file is exhausted.
- [ ] Test file, section 6a (spurious duplicate `D`): after real sync, hold the seconds field
      static for one extra fix on an ordinary day.
- [ ] Test file, section 6b (backwards `D`): after real sync, emit a timestamp a few seconds
      earlier than the last accepted one.
- [ ] Test file, section 6c (leap-second stall `L`, order-dependent): the very first group in the
      file lands exactly on `HH:MM:59` the day before a compiled leap-second date (e.g.
      `2016-12-31 23:59:59`), since the unconditional `clockSet` path only runs once, on the
      first-ever fix after reset; the next group repeats that identical timestamp.
- [ ] Test file, section 7a (Y2036 wraparound): the first group lands a few minutes before
      `2036-02-07 06:28:16 UTC`; subsequent groups relay realistic NMEA traffic with every
      date/time field rebased forward by that same fixed offset, without re-seeding or jumping
      discontinuously again.
- [ ] Run each scenario against the device and confirm the expected serial message
      (`D`/`L`/normal telemetry per `TESTPLAN.md` section 3's reference table) for each one.
