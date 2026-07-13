# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for a Teensy 4.1 + Ethernet + GPS-based NTP server. GPS PPS (pulse-per-second) on pin 35
provides the timing edge; GPS NMEA sentences (ZDA or RMC/GGA) provide the calendar time. A PID
controller disciplines the Teensy's onboard 1588 hardware clock, and the disciplined clock is
served over NTP/UDP via lwIP. A small web server exposes JSON state and an HTML/JS status page
(offset graph, satellite radar).

The `.ino` file is the Arduino entry point; everything else is plain C++ compiled either as part
of the Arduino/Teensy build or against the ArduinoFake mock for host-side unit tests.

## Build / test commands

There is no on-device build step usable outside the Arduino IDE/Teensyduino toolchain (this
requires `https://github.com/ddrown/teensy41_ethernet` installed as an Arduino library). Day-to-day
development and verification happens via the host-side unit tests in `test/`.

**One-time setup** (sibling directory to this repo, i.e. `../../ArduinoFake` relative to `test/`):
```
cd ../..
git clone https://github.com/FabioBatSilva/ArduinoFake.git
cd ArduinoFake
mkdir build && cd build
cmake ..
make
```

**Run all tests:**
```
cd test
make dep all
```
`make dep` regenerates `.depend` (header dependency tracking); `make all` builds every
`test-*.cpp` into its own binary and runs it with `LD_LIBRARY_PATH` pointed at the built
ArduinoFake/Unity shared libs.

**Run a single test binary:**
```
cd test
make test-DateTime
LD_LIBRARY_PATH=../../ArduinoFake/build/src/ ./test-DateTime
```
or `make run-test-DateTime` to build+run in one step.

**Adding a new test file:** name it `test-<Name>.cpp`, add a build rule `test-<Name>:
test-<Name>.o <ProductionFile>.o $(LIBS)` in `test/Makefile` (mirroring the existing
`test-DateTime`/`test-GPS`/etc. rules), and it's picked up automatically by the `$(BINS)` wildcard.

Tests use Unity (`RUN_TEST`/`TEST_ASSERT_*`, `main()` calling `UNITY_BEGIN()`/`UNITY_END()`) plus
ArduinoFake/FakeIt (`using namespace fakeit;`) to mock Arduino APIs (`Serial`, `Stream`, etc.) on
host. `test/lwip/` and `test/lwip_t41.h` are minimal stub headers standing in for the real lwIP
network stack so NTP-adjacent code can compile for tests without a real network stack. Only a
subset of classes have tests (`DateTime`, `GPS`, `NTPClock`, `ClockPID`) — `NTPServer`,
`NTPClients`, `WebServer`, `WebContent`, `InputCapture` are untested and depend directly on
lwIP/Teensy hardware APIs.

`CXX_FLAGS` in `test/Makefile` injects `-DCOUNTERFUNC=micros -DCOUNTSPERSECOND=1000000` (host
clock stand-in for the Teensy's 1588 counter) and `-DNTPPID_KP/KI/KD` overrides for the PID gains
— these are otherwise defined in `platform-clock.h` / `ClockPID.h`.

## Architecture

**Data flow, once per GPS fix:**
1. `InputCapture` (`InputCapture.h/.cpp`) latches the local 1588 hardware counter on each PPS
   rising edge (pin 35 interrupt) → `pps` global.
2. `GPSDateTime` (`GPS.h/.cpp`, global `gps`) incrementally parses NMEA sentences off
   `GPS_SERIAL` character-by-character (`decode()`), tracking a small state machine over
   `$GxZDA`/`$GxRMC`/`$GxGGA`/`$GxGSA`/`$GxGSV` sentence types (checksum-verified). It records
   which local millis a full time-bearing sentence completed at and cross-references that against
   the last PPS capture. `settings.h`'s `GPS_USES_RMC` switches between ZDA-based and RMC+GGA-based
   time decoding for GPS modules that don't emit ZDA. See `GPSnow()` for the resulting `DateTime`.
3. `teensy-ntp.ino`'s `updateTime()` (called from `gps_serial_poll()` in `loop()`) sanity-checks
   the PPS-to-GPS-message lag (rejects >950ms), takes a median of 3 consecutive offset samples to
   reject outliers, then feeds `ClockPID` and `NTPClock`.
4. `ClockPID_c` (`ClockPID.h/.cpp`, global `ClockPID`) is a PID-like discipline loop (gains
   `NTPPID_KP/KI/KD`) that accumulates up to `NTPPID_MAX_COUNT` (16) samples, computes drift via
   Theil-Sen linear regression + chi-squared, and outputs a parts-per-billion correction consumed
   as `localClock.setPpb(...)`.
5. `NTPClock` (`NTPClock.h/.cpp`, global `localClock`) maps the free-running hardware counter
   (`COUNTERFUNC()`/`COUNTSPERSECOND`, defined in `platform-clock.h`, normally the Teensy 1588
   timer at 25MHz) to wall-clock NTP timestamps, applying the current ppb correction.
6. `NTPServer` (`NTPServer.h/.cpp`, global `server`) answers incoming NTP client packets over
   lwIP UDP using `localClock`'s current time, tracking per-client interleaved-mode state via
   `NTPClients` (`NTPClients.h/.cpp`, global `clientList`, fixed pool of `NUMCLIENTS` entries,
   IPv4 or IPv6 depending on `LWIP_IPV6`).
7. `WebContent`/`WebServer` (global `webcontent`/`webserver`) expose current clock/GPS/satellite
   state as JSON (`jsonState()`) and serve the static `index_html.h`/`index_js.h` (pre-generated
   byte arrays of the status page) for the browser-side offset graph and satellite radar.

**Key invariants / gotchas:**
- Most classes are singletons instantiated as globals (`gps`, `localClock`, `ClockPID`, `server`,
  `clientList`, `pps`, `webserver`, `webcontent`) and referenced via `extern` declarations in their
  headers — there's one of each, wired together in `teensy-ntp.ino`.
- Time values move between several epochs: `DateTime` supports seconds-since-2000, NTP
  (since 1900), and Unix (since 1970) via `secondstime()`/`ntptime()`/`unixtime()`.
- `NTPClock`/`NTPServer` represent sub-second time as split 32-bit whole/fractional NTP timestamp
  pairs (`_s`/`_fb` or `s16[2]`/`s32` unions), not floating point, to keep this cheap on the MCU.
- `loop()` interleaves `enet_proc_input()` calls between other polling steps to keep the lwIP
  stack serviced with low latency; preserve that interleaving pattern when adding new polling work
  to `loop()`.
- GPS sentence parsing assumes GGA arrives before RMC within a fix cycle when `GPS_USES_RMC` is
  set (see `GPS.cpp`); the ordering dependency is a known fragile point (see issue references in
  git history for GNRMC/GNGGA handling).
