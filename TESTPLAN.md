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

**Corrected understanding as of `data/run3`/`data/run4` bench sessions** (the paragraph below
this originally predicted repeated `LAG` messages with `ppsToGPS` growing without bound; that
never happened on the bench, and reading `GPS.cpp` closely explains why): `decodeType()`
snapshots `ppsMillis_`/`ppsCounter_`/`dateMillis` *together*, all three gated on
`pps.getCaptures()` having advanced since the last snapshot. With PPS gone, that whole gated
block stops running, so `dateMillis` (what `capturedAt()` returns) freezes at the same instant
as `ppsMillis_`/`ppsCounter_` -- it does **not** keep advancing on each new sentence, so
`ppsToGPS` (their difference) stays constant, not growing. More fundamentally, `commit()`'s
`committedThisPulse_` gate (also tied to `pps.getCaptures()`) means `decode()` stops returning
`true` at all once PPS is lost, so `gps_serial_poll()`'s `if(committed)` branch -- the *only*
place that calls `updateTime()`/`updateTimeCore()`, which is where the `LAG` check and
`discipline.process()` live -- simply never runs again, for any reason, until PPS comes back.
No `LAG` messages, growing or otherwise, and no more `D`/`L`/normal-telemetry lines either.
Holdover detection itself is unaffected by any of this: `slower_poll()`'s `holdover.poll()` is
driven by `millis()` directly, not by `decode()`/`commit()`, so `inHoldover`/dispersion still
work correctly despite the GPS-parsing side being fully stuck.

- [x] Pull the PPS line; confirm **no** repeated `LAG ...` messages appear (see corrected
      understanding above -- confirmed on bench via `data/run3`, `data/run4`: serial output goes
      completely quiet, no `LAG`/`D`/`L`/normal-telemetry lines, until PPS is reconnected)
- [x] After `HOLDOVER_STALE_MS` (4000ms) of no accepted sample, `inHoldover` flips true on
      the next `slower_poll()` cycle (confirmed `data/run3`)
- [x] Web UI / JSON: `inHoldover` true, `holdoverDispersion` growing -- left disconnected
      several minutes in both `data/run3`/`data/run4`, dispersion visibly grows, not just flips
      on (`holdoverElapsedMs` no longer exists as a field -- replaced by `holdoverStartTime`,
      see below)
- [x] NTP responses keep being served (D-only discipline) with the growing dispersion
      reflected to clients (confirmed `data/run3`: dispersion growing "on the web page and NTP
      responses" both, sample client offset/frequency captured right before PPS returned)
- [x] Reconnect PPS: next real pulse + next NMEA sentence gives a matching pair, lag check
      passes, sample accepted, `inHoldover` clears without a clock step (confirmed `data/run3`:
      offset stayed in the tens-of-microseconds range through the resync, `dChiSq` ramping up
      from 0 is the already-verified bootstrap-buffer-reset behavior, not a step)
- [x] "NTP time" in the web UI keeps advancing throughout the outage instead of freezing
      (`WebContent` always reads `localClock`'s live time) -- confirmed `data/run3` ("NTP time in
      web page is advancing"); the diagnostic fields (`offsetHuman`/`pidD`/`dChiSq`) correctly
      *do* stay frozen at their last resolved values, which is the accepted tradeoff, not a
      regression
- [x] "GPS reported time" stays `in sync` throughout, even while `inHoldover` is true -- **first
      found broken** in `data/run3` (froze, same root cause as the `LAG`/telemetry silence
      above: `decode()` never returns `true` again once PPS is lost, and that was the only place
      `setGpsTime()` was called from); fixed by decoupling it from `commit()`'s gate
      (`reportedUpdate()`/`reportedNow()`, see DONE.md). **Confirmed fixed** in `data/run4`
      ("gps reported time stayed 'in sync'" for the whole episode)
- [x] "Holdover started at" shows the last accepted sample's own timestamp plus
      `HOLDOVER_STALE_MS` (4s), and stays *exactly* the same value across repeated polls
      within this one episode rather than drifting -- `data/run4` shows the value
      (`2026-07-25T03:50:49.000Z`); confirmed on bench to stay constant across repeated polls
      through the episode
- [x] Satellite signal counts (`strongSignals`/`weakSignals`/`noSignals`) do **not** grow
      without bound over a sustained holdover -- **found broken** in `data/run4`: `weakSignals`
      reported as 1436 after a ~5 minute holdover (same root-cause class as the GPS-reported-time
      freeze above, but additive instead of freezing: `decodeGSV()`'s
      `strongSignalNext`/`weakSignalNext`/`noSignalNext` only got published-and-reset inside
      `commit()`'s gated block, so they silently accumulated every second of the outage with
      nothing to ever reset them). Fixed by moving the publish/reset to run on every valid
      ZDA/RMC sentence regardless of `commit()`'s gate (see DONE.md, "Satellite signal counts
      accumulating without bound during holdover"). **Confirmed fixed**: ~4 minute holdover
      (2026-07-27T03:38:34.000Z to 2026-07-27T03:42:38.000Z) showed no jump in satellite signal
      counts

This does **not** exercise the `D`/`L` duplicate-timestamp logic (needs fabricated NMEA
content, not a PPS dropout) -- see section 6.

## 5. Web UI

- [x] New holdover fields (`inHoldover`, `holdoverDispersion`, `holdoverStartTime`) update
      live and match serial/JSON
- [x] Existing offset graph / satellite radar still render correctly (new JSON fields
      didn't break the page's JS parser)
- [x] "GPS reported time" shows a real (if not yet locked) date before the first-ever GPS fix,
      distinct from "NTP time" (which reads `localClock`'s live compile-time-seeded clock until
      then) -- confirms the field still serves its original cold-start-visibility purpose.
      Confirmed via a full power cycle of both Teensy and GPS module: "GPS reported time" showed
      an implausible 1970s date first (module hadn't recovered its almanac yet -- this is the
      `secondstime() < compileSecondsTime` "B" case, rejected for discipline but still shown per
      the field's design intent), then quickly jumped to the correct date once the module picked
      up enough almanac data to know the real time (even before it had a full position fix -- it
      briefly showed 2 satellites in view with good signal but no position, suggesting the module
      retained some almanac data across the power cycle)
- [x] During normal steady-state operation "GPS reported time" reads `in sync`, not a raw
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

- [x] Serial prints `D <gpstime>`
- [x] `ClockDiscipline`'s regression/PID state is unaffected (next real sample resumes
      normal telemetry with no discontinuity)

### 6b. Backwards timestamp (`D`)

Once already disciplined: emit a timestamp a few seconds earlier than the last accepted
one.

- [x] Serial prints `D <gpstime>`, rejected unconditionally (not leap-window-dependent)

**Bench note on both 6a/6b's rejection→recovery behavior:** the actual rig feeds a timestamp
offset a fixed 30s from real time (rather than "one extra fix"/"a few seconds earlier" literally),
which sustains the rejection long enough to trigger holdover (6d). Observed: `ClockPID`'s D-only
holdover output was already sitting at `limit_500()`'s ±500ppm clamp (`ClockPID.cpp:126`) during
the whole bad-timestamp run, and after reconnecting real GPS the clock kept running at that same
500ppm clamp to correct back -- net accumulated error was only ~91ms despite the sustained
clamped-rate excursion, not a runaway. Consistent with 6a's still-open "no discontinuity" bullet
(the clamp is what keeps a sustained-rejection episode bounded rather than diverging), but not
literally "no discontinuity" -- it's a bounded, clamped correction, not instantaneous.

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

- [x] Serial prints `L <gpstime>`, stepped forward one second to `2017-01-01 00:00:00`.
      Confirmed via `data/run5` using `rebase_relay.py leap --leap-mode dup59 --leap-at
      2016-12-31T23:59:59 --lead-seconds 180`: serial printed `L 3692217636` at the crossing
      (the exact printed value trails the boundary a little due to `ClockDiscipline`'s normal
      steady-state buffering/resolve cadence, not a bug); more directly, a real NTP client's
      packet capture (`tcpdump`) shows the served Leap Indicator flip from `+1s` (pending) to
      `0` (none) exactly bracketing a response with Receive Timestamp `2017-01-01T00:00:00Z` --
      this is the first time the wire-protocol LI bit itself (not just the serial `L` message)
      was confirmed against a real client
- [x] Resume real dates afterward and confirm no bad step lands in the offset telemetry --
      confirmed `data/run5`: `offsetHuman` stayed in the tens-of-microseconds range across the
      `L` event (0.000000048 → 0.000000068 → 0.000000083), no discontinuity

**Bench setup note:** this run needed `compileSecondsTime`'s "B" bad-clock sanity check
(`gps_serial_poll()`) temporarily disabled, since a normal `__DATE__`/`__TIME__` build's compile
time is long after 2016-12-31 -- see `mitm/README.md`'s note on this and `TODO.md`/`DONE.md` for
how that override was done. That patch is a local, uncommitted, test-only modification to
`teensy-ntp.ino` (hardcodes `compile` to `2016-12-30` and comments out the "B" check/`else`) --
worth reverting before flashing anything meant for real operation.

### 6d. Sustained rejection eventually triggers holdover

Before `98c6c5f`, `holdover.noteSampleReceived()` fired even on a *rejected* sample, so a
long run of `D`s never tripped holdover -- something kept "arriving" every second even
though nothing was trusted. Confirms the fix: keep 6a's or 6b's duplicate/backwards
injection running continuously (not just the one-shot check those sections describe) past
`HOLDOVER_STALE_MS` (4000ms).

- [x] `inHoldover` flips true partway through the sustained `D` run, not never. Confirmed via
      `data/run6` using `mitm/generate.py 6a-long` (a purpose-built long-running duplicate-`D`
      fixture) + `player.py`: serial showed a long run of repeated `D <gpstime>` lines, and the
      web UI/JSON snapshot taken during that run read `inHoldover: yes`
- [x] `holdoverDispersion` grows the longer the rejection run continues -- confirmed on a
      follow-up to `data/run6` (the initial snapshot read `0.000s`, too soon after holdover
      first tripped; a later look showed it growing)
- [-] Left long enough, NTP responses fall back to stratum 16 (`dispersion > 0x10000`) --
      **skipped by decision, not tested**: same underlying `growDispersion()`/threshold logic as
      section 4's PPS-loss holdover, not specific to the sustained-`D` trigger path this section
      covers
- [x] Resuming valid NMEA afterward clears holdover normally (same bar as section 4's PPS
      reconnect check) -- confirmed on the `data/run6` follow-up: offset stayed in the low
      single-digit microseconds through the resync (no step), `dChiSq`/`ppb` ramping up from 0
      is the already-verified bootstrap-buffer-refill behavior, not a fault

### Isolation caveat

While feeding fabricated timestamps the device disciplines its real hardware clock and
answers real NTP requests with whatever was injected -- keep it off any network with real
clients for the duration of section 6. After each spoofing run, **reboot** before trusting
the unit again rather than switching back to real NMEA and hoping the regression
buffer/PID state recovers cleanly -- it will have absorbed the fabricated jumps.

## 7. Y2036 wire-timestamp rollover (NMEA MITM)

The 32-bit NTP wire format (`ntptime()`, seconds since 1900) wraps at **2036-02-07
06:28:16 UTC**. Four independent things need to survive that crossing, and the hijacker can
drive all four live instead of waiting a decade:

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
4. **`LeapSeconds.cpp`'s leap-offset table lookups** -- found via `data/run7`, **still open, not
   yet fixed** (see TODO.md). `leapSecondOffsetAt()`/`leapSecondOffsetAtTai()` have no
   wraparound handling, so the leap-second offset they return silently drops to 0 forever once
   their input wraps -- meaning GPS samples decoded via `ntptime()` are missing their leap
   offset (currently 37s) from that point on, permanently, not just transiently like 1-3 above.
   Also produces a burst of `D <n>` messages right at the crossing as a side effect (see 7e).

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

- [x] No `B <gpstime>` message appears as the rebased clock crosses `06:28:16` -- normal
      telemetry (the plain per-sample line) continues uninterrupted through the crossing.
      Confirmed `data/run7`: no `B` lines at all, including through the later `D`-storm/holdover
      episode (see 7e) -- this check is independent of the leap-offset bug found in that run,
      since it compares `secondstime()`, not anything touching `LeapSeconds.cpp`
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
      client and confirm the served time, not just the fact that a response arrives) --
      **found broken** in `data/run7`: responses kept being served without interruption, but
      built on top of GPS samples silently missing their leap offset (37s) from the crossing
      onward -- see the new "LeapSeconds.cpp" hazard above/TODO.md. Re-test once that's fixed
- [ ] Web UI / JSON offset and dispersion fields stay sane through the crossing -- no spikes
      or stuck/negative values -- **found broken** in `data/run7`: this is exactly what caught
      the leap-offset bug -- `offsetHuman`/"Offset between NTP/GPS times" spiked to ~-37s and
      `ClockPID` pinned at its -500ppm clamp for several minutes correcting (really,
      mis-correcting) it. Re-test once TODO.md's fix lands

### 7e. `ClockDiscipline`'s own monotonicity guard

A third, more severe Y2036 hazard found via this same rebase-relay testing (not one of the
two listed in this section's intro): `ClockDiscipline::process()`'s monotonicity guard
compared raw wrapping `TaiNtpTime.v` values directly (`ClockDiscipline.cpp:48`, pre-`b2a4027`).
Once the rebased clock crossed the wrap, every subsequent real sample compared as
"backwards" and was rejected (`D 0`, `D 1`, ...) -- and since a rejected sample never updates
`lastGpstime_`, it couldn't recover until the *next* 2^32-second wrap. Confirmed on the
bench; fixed via the same `elapsedWithin()` wraparound-safe pattern already used elsewhere
in this section.

- [x] `ClockDiscipline`'s own monotonicity guard (`b2a4027`) is not the cause of any `D`
      messages at the crossing -- confirmed indirectly via `data/run7`: a `D <n>` storm *did*
      appear right at the crossing, but root-caused to the separate, still-open `LeapSeconds.cpp`
      leap-offset bug above (TODO.md), not a regression here. Mechanism: `TaiNtpTime.v` (`=
      wireT + offset`) overflows ~37s *before* `wireT` itself does (adding the offset pushes the
      sum past 2^32 that much earlier), so there's a window where `TaiNtpTime.v` has already
      wrapped but the leap-offset lookup (keyed on the not-yet-wrapped `wireT`) is still correct
      -- clean samples through here. Once `wireT` *itself* wraps, the offset silently drops to 0
      and `TaiNtpTime.v` genuinely jumps back ~37s -- the monotonicity guard correctly rejects
      that as an implausible backward jump (this is what `D <n>` was), exactly as designed,
      until the now-permanently-offset sequence climbs back above the last accepted value and
      gets waved through as an ordinary +1s step. Re-test for a clean crossing (no `D` at all)
      once TODO.md's `LeapSeconds.cpp` fix lands

## 8. Leap-second table currency

- [x] Confirm `LeapSeconds.cpp`'s last entry is still current against IERS Bulletin C (no
      way to test a real upcoming leap second on demand; this is the live-service
      substitute)

## 9. GPS parsing regression soak

- [x] Longer soak (1hr+) on the actual GPS module/sentence set in use, watching for
      checksum failures or decode stalls -- `GPS.cpp` had a String-to-char-buffer rewrite
      and runtime ZDA/RMC/GGA detection earlier in this line of work; the GNRMC/GNGGA
      ordering dependency has historically been fragile here
