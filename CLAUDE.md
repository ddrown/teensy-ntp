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

The on-device build needs `https://github.com/ddrown/teensy41_ethernet` installed as an Arduino
library, plus either the Arduino IDE + Teensyduino plugin, or `arduino-cli` with PJRC's Boards
Manager package index (`teensy:avr` core) -- see `README.md`, "Building and flashing", and this
repo's `compile.sh`/`upload.sh`/`monitor.sh` for the arduino-cli path. Day-to-day development and
verification happens via the host-side unit tests in `test/`, which don't need either toolchain.

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
network stack so NTP-adjacent code can compile for tests without a real network stack. Most
classes have tests (`DateTime`, `GPS`, `NTPClock`, `ClockPID`, `ClockDiscipline`, `ClockHoldover`,
`NTPClients`, `NTPResponseFields`, `LeapSeconds`, `Elapsed`, `InputCapture`) — `NTPServer`,
`WebServer`, `WebContent` are untested and depend directly on lwIP/Teensy hardware APIs.

`CXX_FLAGS` in `test/Makefile` injects `-DCOUNTERFUNC=micros -DCOUNTSPERSECOND=1000000` (host
clock stand-in for the Teensy's 1588 counter) and `-DNTPPID_KP/KI/KD` overrides for the PID gains
— these are otherwise defined in `platform-clock.h` / `ClockPID.h`.

## Architecture

**Data flow, once per GPS fix:**
1. `InputCapture` (`InputCapture.h/.cpp`) latches the local 1588 hardware counter on each PPS
   rising edge (pin 35 interrupt) → `pps` global.
2. `GPSDateTime` (`GPS.h/.cpp`, global `gps`) incrementally parses NMEA sentences off
   `GPS_SERIAL` character-by-character (`decode()`), tracking a small state machine over
   `$GxZDA`/`$GxRMC`/`$GxGGA`/`$GxGSA`/`$GxGSV` sentence types (checksum-verified, silently
   dropped on failure). Sentence type and arrival order (ZDA vs. RMC+GGA, whichever a given GPS
   module actually emits) are detected at runtime rather than a `settings.h` compile-time switch --
   whichever of ZDA/RMC/GGA is first to arrive after a given `InputCapture::getCaptures()` tick is
   the one used to snapshot the PPS/local-time reference for that fix. See `GPSnow()` for the
   resulting `DateTime`.
3. `teensy-ntp.ino`'s `updateTime()` (called from `gps_serial_poll()` in `loop()`) sanity-checks
   the PPS-to-GPS-message lag (rejects >950ms) and calls `ClockDiscipline::process()`
   (`ClockDiscipline.h/.cpp`, global `discipline`), which owns the median-of-3 outlier rejection
   and duplicate/backwards-timestamp/leap-second-stall handling, then feeds `ClockPID` and
   `NTPClock`.
4. `ClockPID_c` (`ClockPID.h/.cpp`, global `ClockPID`) is a PID-like discipline loop (gains
   `NTPPID_KP/KI/KD`) that accumulates up to `NTPPID_MAX_COUNT` (16) samples, computes drift via
   Theil-Sen linear regression + chi-squared, and outputs a parts-per-billion correction consumed
   as `localClock.setPpb(...)`.
5. `ClockHoldover` (`ClockHoldover.h/.cpp`, global `holdover`) watches for GPS/PPS going silent
   (`HOLDOVER_STALE_MS`, 4s) and takes over: switches discipline to `ClockPID`'s drift-rate (D)
   term alone, and grows a dispersion estimate over time that eventually pushes `NTPServer` to
   report stratum 16 -- see `TODO.md`/`DONE.md`, "GPS lock lost / PPS stopped", and `TESTPLAN.md`
   section 4.
6. `NTPClock` (`NTPClock.h/.cpp`, global `localClock`) maps the free-running hardware counter
   (`COUNTERFUNC()`/`COUNTSPERSECOND`, defined in `platform-clock.h`, normally the Teensy 1588
   timer at 25MHz) to wall-clock NTP timestamps, applying the current ppb correction.
7. `NTPServer` (`NTPServer.h/.cpp`, global `server`) answers incoming NTP client packets over
   lwIP UDP using `localClock`'s current time, tracking per-client interleaved-mode state via
   `NTPClients` (`NTPClients.h/.cpp`, global `clientList`, fixed pool of `NUMCLIENTS` entries,
   IPv4 or IPv6 depending on `LWIP_IPV6`). Stratum/leap-indicator selection lives in
   `NTPResponseFields.h/.cpp`. Both incoming and outgoing packets are hardware-timestamped by the
   ENET MAC (not software-timed); interleaved mode exists to deliver the real hardware transmit
   timestamp, which isn't known until after a packet has actually gone out -- see `README.md`,
   "Hardware timestamping".
8. `WebContent`/`WebServer` (global `webcontent`/`webserver`) expose current clock/GPS/satellite
   state as JSON (`jsonState()`) and serve the static `index_html.h`/`index_js.h` (pre-generated
   byte arrays of the status page) for the browser-side offset graph and satellite radar. Before
   the first real GPS timestamp arrives, the reported `gpstime` falls back to `localClock`'s live
   (compile-time-seeded) clock rather than sitting at zero, so the graphs don't appear stuck at
   the NTP epoch across a power cycle.

**Key invariants / gotchas:**
- Most classes are singletons instantiated as globals (`gps`, `localClock`, `ClockPID`,
  `discipline`, `holdover`, `server`, `clientList`, `pps`, `webserver`, `webcontent`) and
  referenced via `extern` declarations in their headers — there's one of each, wired together in
  `teensy-ntp.ino`.
- Time values move between several epochs: `DateTime` supports seconds-since-2000, NTP
  (since 1900), and Unix (since 1970) via `secondstime()`/`ntptime()`/`unixtime()`.
- `NTPClock`/`NTPServer` represent sub-second time as split 32-bit whole/fractional NTP timestamp
  pairs (`_s`/`_fb` or `s16[2]`/`s32` unions), not floating point, to keep this cheap on the MCU.
- `loop()` interleaves `enet_proc_input()` calls between other polling steps to keep the lwIP
  stack serviced with low latency; preserve that interleaving pattern when adding new polling work
  to `loop()`.
