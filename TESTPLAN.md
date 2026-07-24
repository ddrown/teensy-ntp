# Test plan

Verification plan for the current branch's changes: `ClockDiscipline` extraction, GPS
holdover, leap-second handling, Y2036/wraparound safety, and duplicate/backwards GPS
timestamp rejection. Checklist form -- tick items off during the bench session. Items
under "Live NMEA-hijacker tests" depend on the MITM NMEA rig described there; everything
above it only needs the two-Teensy comparison setup.

## 1. Build & unit tests

- [x] `cd test && make dep all` -- full suite green (`test-DateTime`, `test-GPS`,
      `test-ClockDiscipline`, `test-LeapSeconds`, `test-NTPClients`, `test-NTPServer`,
      `test-ClockPID`, `test-InputCapture`)
- [x] Arduino/Teensyduino compile clean, flashes, boots, gets a DHCP lease, GPS acquires a
      fix

## 2. Baseline dual-Teensy comparison

- [x] New firmware on one Teensy, old firmware on a second, both with GPS/PPS; compare
      served time against each other via a local NTP client
- [x] Let it run past several `ClockDiscipline` resolve cycles (steady state buffers 2
      samples then resolves on the 3rd) and confirm offset/ppb tracks the old firmware
- [x] Watch the normal per-sample serial line (`pps offsetHuman pidD dChiSq ppb gpstime`)
      for sane, not just present, values

## 3. Serial console message reference

Know these before watching the console, not just recognize them after the fact:

| Prefix | Meaning | Source |
|---|---|---|
| `S <pps> <gpstime>` | clock-set, once at startup | `discipline.process()` `clockSet` |
| `<pps> <offset> <pidD> <dChiSq> <ppb> <gpstime>` | normal steady-state telemetry | `updateTime()` |
| `LAG <ppsToGPS> <ppsMillis> <gpstime>` | PPS-to-NMEA lag > 950ms | pre-`discipline.process()` gate |
| `D <gpstime>` | duplicate or backwards GPS timestamp rejected | `ClockDiscipline` monotonicity guard |
| `L <gpstime>` | GPS stalled instead of emitting `:60`, corrected forward | `ClockDiscipline` + `LeapSeconds::leapSecondStallSecond()` |
| `B <gpstime>` | GPS clock rejected as bad (`secondstime() < compileSecondsTime`) | `gps_serial_poll()`, `teensy-ntp.ino` |

- [x] Confirm a healthy GPS never spuriously prints `D` or `L` over a soak (false-positive
      check)

## 4. GPS holdover -- disconnect PPS (not the antenna/serial)

Disconnecting PPS specifically exercises the `LAG`-then-holdover path cleanly: `GPS.cpp`
only snapshots `ppsMillis_`/`ppsCounter_` from `InputCapture` once per completed NMEA
sentence, so with PPS gone the snapshot freezes while `capturedAt()` keeps advancing on
each new sentence -- `ppsToGPS` grows ~1000ms/sec and blows past the 950ms threshold almost
immediately, so `discipline.process()` never runs and never resets the holdover staleness
timer.

- [ ] Pull the PPS line; confirm serial prints repeated `LAG ...` with growing `ppsToGPS`
- [ ] After `HOLDOVER_STALE_MS` (4000ms) of no accepted sample, `inHoldover` flips true on
      the next `slower_poll()` cycle
- [ ] Web UI / JSON: `inHoldover` true, `holdoverDispersion` growing, `holdoverElapsedMs`
      counting up -- leave it disconnected a couple minutes to see the dispersion actually
      *grow*, not just flip on
- [ ] NTP responses keep being served (D-only discipline) with the growing dispersion
      reflected to clients (`ntpq -c rv` or equivalent)
- [ ] Reconnect PPS: next real pulse + next NMEA sentence gives a matching pair, lag check
      passes, sample accepted, `inHoldover` clears without a clock step
- [ ] "NTP time" in the web UI keeps advancing throughout the outage instead of freezing
      (`WebContent` always reads `localClock`'s live time) -- the diagnostic fields
      (`offsetHuman`/`pidD`/`dChiSq`) correctly *do* stay frozen at their last resolved values,
      which is the accepted tradeoff, not a regression
- [ ] "GPS reported time" stays `in sync` throughout, even while `inHoldover` is true -- NMEA
      sentences keep decoding independent of the PPS capture snapshot, so this and "NTP time"
      answer different questions ("is GPS still talking" vs "when did the device last trust a
      sample")
- [ ] "Holdover started at" shows the last accepted sample's own timestamp plus
      `HOLDOVER_STALE_MS` (4s), and stays *exactly* the same value across repeated polls
      within this one episode rather than drifting

This does **not** exercise the `D`/`L` duplicate-timestamp logic (needs fabricated NMEA
content, not a PPS dropout) -- see section 6.

## 5. Web UI

- [x] New holdover fields (`inHoldover`, `holdoverDispersion`, `holdoverElapsedMs`) update
      live and match serial/JSON
- [x] Existing offset graph / satellite radar still render correctly (new JSON fields
      didn't break the page's JS parser)
- [ ] "GPS reported time" shows a real (if not yet locked) date before the first-ever GPS fix,
      distinct from "NTP time" (which reads `localClock`'s live compile-time-seeded clock until
      then) -- confirms the field still serves its original cold-start-visibility purpose
- [ ] During normal steady-state operation "GPS reported time" reads `in sync`, not a raw
      timestamp, confirming the ~1s NMEA-cadence jitter doesn't look like a fault

## 6. Live NMEA-hijacker tests

Requires a MITM sitting on the GPS→Teensy UART (`Serial1`) only -- leave the physical PPS
line untouched so real pulse timing stays intact. Two hard requirements or the target code
never gets reached:

- **Checksum**: `GPS.cpp` verifies the NMEA checksum; any edited sentence needs a
  recomputed XOR checksum or it's silently dropped before decode.
- **Timing**: the 950ms PPS-to-NMEA lag check is still live; a rewritten sentence must land
  within that window of the real PPS edge it corresponds to.

Check which sentence type the GPS module actually emits (ZDA, or RMC+GGA) -- sentence
detection is runtime-based, so either works, just craft the one that's really sent.

### 6a. Spurious duplicate timestamp (`D`)

Once already disciplined on real time: hold the seconds field static for one extra fix, on
an ordinary day (not near any compiled leap-second date). Equality branch fires,
`leapSecondStallSecond()` returns false, sample rejected.

- [ ] Serial prints `D <gpstime>`
- [ ] `ClockDiscipline`'s regression/PID state is unaffected (next real sample resumes
      normal telemetry with no discontinuity)

### 6b. Backwards timestamp (`D`)

Once already disciplined: emit a timestamp a few seconds earlier than the last accepted
one.

- [ ] Serial prints `D <gpstime>`, rejected unconditionally (not leap-window-dependent)

### 6c. Leap-second stall correction (`L`) -- order-dependent, read carefully

The monotonicity guard only runs once `settime_` is already true; the very first sample
after a reset takes an unconditional clock-set path that skips the guard and adopts
whatever timestamp it's given as `lastGpstime_`. Jumping to a historical date *after* the
clock is already running on real time will just get rejected as an ordinary backwards jump
and never reach the stall check -- rejected samples don't update `lastGpstime_`, so retrying
doesn't help.

Correct sequence:
1. Power-cycle (or otherwise get to the point right before first GPS lock)
2. Make the **first-ever** spoofed fix land exactly on `HH:MM:59` the day before a compiled
   leap-second date -- e.g. `2016-12-31 23:59:59` (table's last entry, effective
   `2017-01-01`; see `LeapSeconds.cpp`). This becomes `lastGpstime_` unconditionally via the
   `clockSet` path.
3. Repeat that identical timestamp on the next fix.

- [ ] Serial prints `L <gpstime>`, stepped forward one second to `2017-01-01 00:00:00`
- [ ] Resume real dates afterward and confirm no bad step lands in the offset telemetry

### 6d. Sustained rejection eventually triggers holdover

Before `98c6c5f`, `holdover.noteSampleReceived()` fired even on a *rejected* sample, so a
long run of `D`s never tripped holdover -- something kept "arriving" every second even
though nothing was trusted. Confirms the fix: keep 6a's or 6b's duplicate/backwards
injection running continuously (not just the one-shot check those sections describe) past
`HOLDOVER_STALE_MS` (4000ms).

- [ ] `inHoldover` flips true partway through the sustained `D` run, not never
- [ ] `holdoverDispersion` grows the longer the rejection run continues
- [ ] Left long enough, NTP responses fall back to stratum 16 (`dispersion > 0x10000`)
- [ ] Resuming valid NMEA afterward clears holdover normally (same bar as section 4's PPS
      reconnect check)

### Isolation caveat

While feeding fabricated timestamps the device disciplines its real hardware clock and
answers real NTP requests with whatever was injected -- keep it off any network with real
clients for the duration of section 6. After each spoofing run, **reboot** before trusting
the unit again rather than switching back to real NMEA and hoping the regression
buffer/PID state recovers cleanly -- it will have absorbed the fabricated jumps.

## 7. Y2036 wire-timestamp rollover (NMEA MITM)

The 32-bit NTP wire format (`ntptime()`, seconds since 1900) wraps at **2036-02-07
06:28:16 UTC**. Three independent things need to survive that crossing, and the hijacker can
drive all three live instead of waiting a decade:

1. **`gpstime`-vs-`compileTime` sanity check** (`gps_serial_poll()`, `teensy-ntp.ino:224`).
   Before `817ce85`, this compared raw `ntptime()` values; after a real-world wrap, a
   correctly-encoded post-wrap `gpstime` numerically aliases back down near 0 while
   `compileTime` (fixed at build time, pre-wrap, a large raw value) doesn't move --
   `gpstime < compileTime` would then evaluate true forever, permanently rejecting every
   real GPS fix as bad with no recovery short of a rebuild. The fix compares
   `DateTime::secondstime()` (seconds-since-2000, monotonic to ~2136) instead. On rejection
   the firmware prints `B <gpstime>` ("gps clock bad").
2. **`NTPClients::expireClients()`** (`NTPClients.cpp`) -- compares `nowWire` against each
   client's registered `rx_s`, both raw wire-format seconds, via the wraparound-safe
   `elapsedWithin()` helper rather than plain subtraction, so a client that registered just
   before the wrap shouldn't look expired (or falsely fresh) for the ~4096s window
   straddling it.
3. **`ClockDiscipline::process()`'s own monotonicity guard** (`ClockDiscipline.cpp:48`) --
   found later than the other two, via this same rebase-relay testing; see 7e.

### 7a. Seeding near the wrap instead of waiting a decade

Same trick as the leap-second test (6c): the very first sample after a reset takes the
unconditional `clockSet` path, so you can start the device's disciplined clock arbitrarily
close to the wrap instant rather than walking it there from today. From then on, just relay
real GPS pulses/NMEA with the date/time fields rebased forward by a fixed offset (today →
your chosen start time) -- real PPS pulses keep arriving once a second, so the rebased
clock crosses the wrap for real within minutes of bench time.

1. Power-cycle (or get to the point right before first GPS lock)
2. Make the first-ever spoofed fix land a few minutes before `2036-02-07 06:28:16 UTC`,
   e.g. `06:25:00`
3. From there, keep relaying real NMEA traffic with every date/time field rebased by that
   same fixed offset -- do not re-seed or jump discontinuously again, or you'll trip the
   backwards/duplicate guard from section 6a/6b instead of testing the wrap

### 7b. `secondstime()` sanity check

- [ ] No `B <gpstime>` message appears as the rebased clock crosses `06:28:16` -- normal
      telemetry (the plain per-sample line) continues uninterrupted through the crossing
- [ ] Optional regression proof: build `817ce85^` (the commit immediately before the
      `secondstime()` fix) and repeat -- confirm `B <gpstime>` *does* start appearing at the
      crossing and never clears, then confirm current firmware doesn't reproduce it

### 7c. `expireClients()` wraparound safety

1. A few seconds before the crossing, send one real NTP request to the device from a
   client on the bench (`ntpdate -q`, `chronyd -q`, or similar) so it registers in
   `NTPClients` with an `rx_s` just before the wrap
2. Let the rebased clock cross the wrap without that client sending anything else
3. - [ ] Confirm (via `WebContent`/serial, or by querying again) that client is **not**
        expired immediately after the crossing -- it should stay registered until ~4096s
        of (rebased) device time have actually elapsed since its `rx_s`, not be treated as
        either instantly stale or permanently fresh by the wrap
4. - [ ] Let rebased time continue past that 4096s window without further requests from
        that client and confirm it **does** eventually expire normally

### 7d. General sanity through the crossing

- [ ] NTP responses stay correct and continuous through the crossing (query with a real
      client and confirm the served time, not just the fact that a response arrives)
- [ ] Web UI / JSON offset and dispersion fields stay sane through the crossing -- no spikes
      or stuck/negative values

### 7e. `ClockDiscipline`'s own monotonicity guard

A third, more severe Y2036 hazard found via this same rebase-relay testing (not one of the
two listed in this section's intro): `ClockDiscipline::process()`'s monotonicity guard
compared raw wrapping `TaiNtpTime.v` values directly (`ClockDiscipline.cpp:48`, pre-`b2a4027`).
Once the rebased clock crossed the wrap, every subsequent real sample compared as
"backwards" and was rejected (`D 0`, `D 1`, ...) -- and since a rejected sample never updates
`lastGpstime_`, it couldn't recover until the *next* 2^32-second wrap. Confirmed on the
bench; fixed via the same `elapsedWithin()` wraparound-safe pattern already used elsewhere
in this section.

- [ ] No `D` messages appear as the rebased clock crosses `06:28:16` -- normal per-sample
      telemetry continues uninterrupted through the crossing, same bar as 7b

## 8. Leap-second table currency

- [x] Confirm `LeapSeconds.cpp`'s last entry is still current against IERS Bulletin C (no
      way to test a real upcoming leap second on demand; this is the live-service
      substitute)

## 9. GPS parsing regression soak

- [x] Longer soak (1hr+) on the actual GPS module/sentence set in use, watching for
      checksum failures or decode stalls -- `GPS.cpp` had a String-to-char-buffer rewrite
      and runtime ZDA/RMC/GGA detection earlier in this line of work; the GNRMC/GNGGA
      ordering dependency has historically been fragile here
