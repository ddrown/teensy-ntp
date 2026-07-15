# TODO

Notes from a code review, roughly ordered by priority.

## GPS lock lost / PPS stopped (open issue)

- [x] **Design: degrade gracefully via D-only holdover + growing estimated dispersion, instead
      of (or in addition to) a hard watchdog timeout.** Preferred shape, discussed 2026-07-13:
      Step 1 done 2026-07-14: extracted the median-of-3/PID-feed/dispersion orchestration that
      used to live inline in `teensy-ntp.ino`'s `updateTime()` into a new `ClockDiscipline` class
      (`ClockDiscipline.h/.cpp`, global `discipline`), constructed with pointers to `localClock`
      and `ClockPID` (same DI pattern `NTPServer` already uses). `updateTime()` now just does the
      PPS-to-GPS lag check and calls `discipline.process(pps, gpstime)`, using the returned
      `DisciplineResult` (`clockSet`/`updated`/`ppb`/`dispersion`/...) to decide what to log and
      push to `server`/`webcontent`. No behavior change -- `median()`/`ntp64_to_32()` and the
      bootstrap-vs-full-PID resolve cadence were moved verbatim. This was previously *zero*-percent
      covered by tests (only reachable via the untested `.ino`); `test/test-ClockDiscipline.cpp`
      now covers the initial clock-set, the bootstrap phase (resolves every call before the PID
      has 16 samples), the steady-state buffer-2-then-resolve-on-3rd cadence once full, the
      median-of-3 selection (two arrival orders), and that localClock/dispersion are only touched
      on a resolving call, not the two buffering ones. This is the seam the holdover feature
      itself (D-only ppb output, growing dispersion during an outage) will plug into next, along
      with the wraparound-safe elapsed-time helper called out below.
      - While samples are arriving normally, discipline the clock with the full P+I+D output as
        today (`ClockPID.out()`, ClockPID.cpp:166 → `localClock.setPpb()`, teensy-ntp.ino:188).
      - Once PPS/GPS samples stop arriving (detected via the existing lag/staleness signal),
        stop trusting P and I — they're phase/accumulated-offset terms and go stale without
        fresh offset samples — and drive `localClock.setPpb()` from the D term alone
        (`ClockPID_c::d_out()`, already exposed at ClockPID.h:39/ClockPID.cpp:161,164) as a pure
        frequency holdover using the last-known drift-rate estimate, rather than freezing the
        combined P+I+D output as happens implicitly today.
      - While in holdover, periodically (e.g. once/sec from `slower_poll()`,
        teensy-ntp.ino:220) grow an *estimated* dispersion as a function of elapsed holdover
        time (NTP's usual model: `dispersion += PHI * elapsed`, PHI a fixed ppm-ish
        worst-case-drift-uncertainty constant) and keep calling `server.setDispersion()` with
        the growing value — instead of leaving `dispersion` frozen at its last real value like
        today.
      - No new stratum-16 mechanism needed: `NTPServer::recv()` already downgrades to stratum 16
        once `dispersion.s32 > 0x10000` (NTPServer.cpp:50) — that check just needs to be fed a
        continuously-growing estimate during holdover instead of a stale frozen one, and the
        existing threshold does the "eventually fall back to unsynced" step for free.
      - This is a nicer shape than a blunt timeout because it reuses NTP's actual
        quality-of-sync channel (dispersion → stratum) instead of adding an unrelated ad hoc
        timer, and it degrades smoothly (still frequency-correct for a while after PPS loss)
        rather than snapping straight from "fully synced" to "unsynced."
      - Still needs the elapsed-time tracking called out below (last successful sample time,
        wraparound-safe) to know how long holdover has lasted and how fast to grow dispersion.
      Step 2 done 2026-07-14: added `ClockHoldover` (`ClockHoldover.h/.cpp`, global `holdover`),
      constructed with the same `NTPClock*`/`ClockPID_c*` DI as `ClockDiscipline`. `updateTime()`
      calls `holdover.noteSampleReceived(millis())` on every accepted sample (regardless of
      whether it resolved into a PID update -- resolves only happen every 3rd call once the PID is
      full, but any accepted sample is evidence GPS/PPS is alive) and `holdover.noteDispersion()`
      whenever a resolve produces a fresh real dispersion value; `slower_poll()` now calls
      `holdover.poll(millis())` about once/sec and pushes `server.setDispersion()` with the
      returned value whenever holdover is active. Inside `poll()`: staleness is a plain
      `elapsedWithin(now, lastGood, HOLDOVER_STALE_MS, ...)` check (both failure modes of that
      call -- genuinely stale, or an ambiguous/wrapped gap -- correctly mean "not confirmed
      fresh"); once stale, `localClock.setPpb()` is driven from `ClockPID.d_out()` alone instead
      of `out()`; holdover duration is accumulated incrementally as bounded per-poll-tick deltas
      (`elapsedWithin(now, lastPoll, HOLDOVER_POLL_MAX_GAP_MS, ...)`, dropped rather than added if
      a tick's gap is implausibly large) rather than a single subtraction across the whole outage,
      per the wraparound-safety rule in `Elapsed.h`; dispersion grows off that duration via a
      `HOLDOVER_PHI_PPM`-per-second model, base-lined from the last real dispersion
      (`noteDispersion()`), and saturates instead of wrapping if it somehow ran long enough to
      approach `uint32_t` overflow. `test/test-ClockHoldover.cpp` covers: no holdover before the
      first-ever sample; staying fresh through the inclusive `HOLDOVER_STALE_MS` boundary; that
      holdover drives `d_out()`-only ppb rather than the full P+I+D `out()` (seeded so the two
      genuinely differ, so this would catch a regression back to the full output); dispersion
      growing monotonically over repeated ~1/sec polls; a single abnormally-large poll-to-poll gap
      not inflating dispersion; and a fresh sample arriving mid-holdover fully resetting state
      (leaves holdover immediately, and a subsequent stale episode grows from the new baseline
      instead of resuming the old accumulated duration). The existing `ppsToGPS` lag check
      (teensy-ntp.ino, item below) is untouched and still uses raw subtraction -- it wasn't the
      target of this step, since GPS going fully silent never reaches it at all (no new NMEA
      sentence means `gps_serial_poll()` never calls `updateTime()`); `ClockHoldover` detects that
      case independently via its own polling, not by reusing that check.
      `HOLDOVER_STALE_MS` raised from 2000 to 4000 after review: a single transient `ppsToGPS`
      lag rejection (already tolerated as normal jitter, not an outage) doubles the gap between
      accepted samples to ~2000ms on its own, and `poll()` runs on its own unsynchronized ~1Hz
      cycle from `slower_poll()`, adding up to another ~1000ms of phase jitter -- 4000ms leaves
      margin above that combined worst case.
      A second wraparound gap found by review (not a test) before this shipped:
      `holdoverElapsedMs_` itself was a plain `uint32_t` millisecond accumulator with no bound,
      so an outage lasting past ~49.7 days would wrap it back to a small value, in turn making
      `growDispersion()` compute a small dispersion again -- silently reporting "looks synced"
      after a holdover measured in weeks, the same failure class `Elapsed.h` exists to prevent,
      just relocated to a new counter. Fixed by adding `saturatingAddMs()` to `Elapsed.h/.cpp`
      (pins at `UINT32_MAX` instead of wrapping) and using it for the `holdoverElapsedMs_ +=`
      accumulation instead of a plain `+=`; `test/test-Elapsed.cpp` adds 3 tests for it (normal
      case, sum landing exactly on `UINT32_MAX` via the non-saturating path, and the actual
      near-the-top-of-range case that must cap rather than wrap).
      Surfaced in the status UI 2026-07-14: `HoldoverStatus` gained an `elapsedMs` field
      alongside `inHoldover`/`dispersion`; `slower_poll()` pushes all three into `WebContent` via
      a new `setHoldover()` (same push pattern as `setPPSData`/`setLocalClock`), which adds them
      to `jsonState()`. `index_html.h`/`index_js.h` render them as "In holdover" (yes/no),
      "Holdover dispersion estimate" (converted from NTP-short 16.16 fixed-point to seconds), and
      "Holdover elapsed" (ms converted to seconds) -- previously `dispersion` wasn't exposed to
      the dashboard at all, and during an actual holdover episode the rest of the page (`clockPpb`,
      `pidD`, etc.) just froze at its last value with no indication the clock had switched to
      D-only free-running.
- [x] **The existing lag check is wraparound-unsafe for long outages.**
      `ppsToGPS = gps.capturedAt() - gps.ppsMillis()` (teensy-ntp.ino:161) relies on unsigned
      subtraction of two `millis()` values, which only gives a correct "looks small" result if
      the true gap is under ~24.8 days (half of the 32-bit `millis()` wrap period, ~49.7 days).
      If PPS is dead for longer than that, the wrapped subtraction can come back around into
      "looks fresh" territory and silently pass the `> 950` check again. Any new watchdog logic
      needs its own explicit bounded-window wraparound-safe comparison — it can't reuse this
      same subtraction trick unmodified if it's meant to catch outages that can run indefinitely.
      Fixed 2026-07-14: the accept/reject decision in `updateTime()` now goes through
      `elapsedWithin(gps.capturedAt(), gps.ppsMillis(), 950, &validatedLag)` instead of raw
      subtraction + `> 950` -- same 950ms boundary, inclusive at exactly 950 either way, so no
      behavior change in the normal case, but now provably rejects a wrapped-around gap instead of
      risking it looking "fresh". The raw subtraction (`ppsToGPS`) is kept alongside, unchanged,
      purely for the `webcontent`/`Serial` "LAG" diagnostic display -- that's cosmetic (a human
      glancing at a wrapped-small number on the status page), not a correctness decision, so it
      wasn't worth threading the validated value through display too. (In practice this exact
      divergence is hard to trigger given how `GPSDateTime::decodeType()` couples
      `ppsMillis_`/`dateMillis` together -- both only update inside the same
      `pps.getCaptures()`-gated block -- so they tend to freeze as a pair rather than one running
      ahead of the other; fixed anyway for defense in depth and to close out the loose end noted
      below.)
- [ ] **Mixed timestamp domains make this easy to get wrong twice.** The codebase compares/
      subtracts across at least three different clock domains without a shared abstraction:
      `millis()` (1 kHz, wraps ~49.7 days — used by `InputCapture`/`GPSDateTime` for
      `dateMillis`/`ppsMillis_`), the hardware 1588/`COUNTERFUNC()` counter (wraps much sooner —
      see the existing comment "`(2+1)*16=48s, 80MHz wraps at 53s`", teensy-ntp.ino:189), and
      GPS-native NTP seconds. Before adding staleness detection, worth introducing one
      wraparound-safe "elapsed(now, then)" helper with an explicit max-valid-window, used
      everywhere a duration is computed, rather than continuing to sprinkle ad hoc unsigned
      subtraction around — otherwise the watchdog fix risks introducing its own wraparound bug.
      Helper added 2026-07-14: `elapsedWithin(now, then, maxWindow, uint32_t *elapsedOut)`
      (`Elapsed.h/.cpp`, no state, no Arduino dependency) returns the forward gap from `then` to
      `now` only if it's within `[0, maxWindow]`, rejecting both an implausibly large gap and the
      "`then` is actually ahead of `now`" case (which unsigned-wraps to a huge gap rather than a
      small negative one). Explicitly does *not* solve unbounded-outage detection by itself — a
      gap of `maxWindow + k*2^32` is indistinguishable from a gap of `maxWindow` from a single
      `(now, then)` pair; callers tracking an open-ended outage still need to poll more often than
      `maxWindow` and accumulate elapsed time incrementally rather than taking one subtraction
      across the whole outage (documented in the header). `test/test-Elapsed.cpp` covers the
      normal case, the exact-boundary-is-valid case, just-past-boundary rejection, a real wrap
      (`then` near `UINT32_MAX`, `now` just past `0`), `then` ahead of `now`, and a long-outage
      gap. Wired into `ClockHoldover`'s staleness and per-poll-tick duration accumulation (see the
      design item above) and, as of 2026-07-14, the existing `ppsToGPS` lag check site too (see
      the item above) -- no known remaining raw *millis()-domain* duration subtraction outside
      `Elapsed.h` itself and `webcontent`'s cosmetic display value. Not yet applied to the
      NTP-seconds domain: `NTPClients::expireClients()` (NTPClients.cpp:55) still computes
      `expire_time = sec - 4096` via raw subtraction on NTP seconds, not milliseconds -- same
      general risk class, different domain/wrap period, untouched by this round.

## Bugs / fragile areas

- [x] Rewrite `GPSDateTime::decodeType()` (GPS.cpp:96) — the `#ifdef GPS_USES_RMC` branch
      interleaves an `if/else if` chain across preprocessor boundaries (RMC/GGA branch vs. ZDA
      branch are stitched together by the preprocessor). This shape produced issue #6 ("code
      assumes RMC comes after GGA") and contributed to #5 ("checksums in the range 0x0..0xF
      fail"). Consider a table/struct-driven parser: an array of
      `{sentence_code, field_count, handler_fn}` selected at *runtime* based on which sentences
      are actually seen, rather than selected at compile time. Would also allow supporting
      ZDA-only, RMC+GGA, or both without recompiling.
      Went with the runtime-detection option rather than just de-interleaving the `#ifdef`s:
      `GPS_USES_RMC`/`GPS_GGA_IS_FIRST` are gone entirely. `validCode`'s `inTimeCode` state is
      now split into `inZDATimeCode`/`inRMCTimeCode`, and `decodeTimeCode()` dispatches on that
      instead of a compile-time macro, so ZDA and RMC+GGA are both handled by the same binary.
      The PPS/millis reference is captured by `decodeType()` on whichever of ZDA/RMC/GGA is
      first to arrive *after a given PPS pulse* — identified via the previously-unused
      `InputCapture::getCaptures()` (an ISR-incremented pulse count), not by assuming a fixed
      sentence order. First attempt used a "reset after the time-bearing sentence completes"
      flag instead of the pulse-count signal; that regressed silently for RMC-before-GGA
      ordering (GGA would re-trigger and overwrite RMC's capture) since sentence order within a
      cycle isn't fixed. Caught by `test_rmc_then_gga_captures_at_rmc` before it shipped.
      Added `test/test-GPS.cpp` coverage that was previously impossible without a separate
      compile: `test_rmc_alone`, `test_gga_then_rmc_captures_at_gga`,
      `test_rmc_then_gga_captures_at_rmc`, `test_capture_resets_on_next_pulse` — all run in the
      default `test-GPS` binary now instead of only being checked with a standalone `g++ -c` of
      the untested `GPS_USES_RMC`/`GPS_GGA_IS_FIRST` build configs. Verified the regression tests
      are meaningful by reverting the pulse-count guard locally: 3 of the 4 new tests failed as
      expected, confirming they'd have caught the original issue #6 bug class.
      Runtime detection also opened a new failure mode the old compile-time macros structurally
      prevented: a module emitting *both* ZDA and RMC in the same cycle (some do) would have
      both independently reach `decodeType()`'s time-bearing branches and both call `commit()`,
      making `decode()` report the same PPS pulse as two separate updates (double-feeding one
      fix into `updateTime()`'s median-of-3 and `ClockPID`'s sample accumulation). Caught by
      review, not by a test, before it shipped. Fixed with a `committedThisPulse_` guard
      (re-armed on the same "new pulse" signal as the capture logic) so only the first
      time-bearing sentence to complete commits; added `test_zda_then_rmc_only_first_commits`
      and `test_rmc_then_zda_only_first_commits`, both confirmed to fail without the guard.
- [x] Double check checksum parsing (`strtoul(tmp.c_str(), NULL, 16)`, GPS.cpp:252) for the
      0x0..0xF class of bug — likely inconsistent handling of "0" vs "00" style hex strings.
      Confirmed already fixed (commit af31793) and confirmed `test/test-GPS.cpp`'s
      `test_checksum` genuinely catches a regression here (verified by reverting the fix
      locally and re-running). Strengthened the test to also cover a non-zero single-hex-digit
      checksum (0x01, previously only 0x00 was covered) since the reported bug was a range,
      not a single value.
- [x] `DateTime::time()` (DateTime.cpp:44-68) leap-year check only tests `year_ % 4 == 0`, while
      `date2days()` (DateTime.cpp:12) correctly excludes century years (`% 100 != 0` unless
      `% 400 == 0`). These will diverge at year 2100. Low priority given device lifespan, but
      worth a comment either way.

## Cleanup

- [x] `DateTime::DateTime(uint32_t)` (DateTime.cpp:40) and `DateTime::time(uint32_t)`
      (DateTime.cpp:71) are byte-for-byte duplicated — have the constructor call `time()`.
- [x] Replace `String`-based accumulation in `GPSDateTime::decode()` (GPS.cpp:207-280,
      `tmp`/`msg`) with a fixed-size char buffer. NMEA sentences have a bounded max length
      (82 chars per spec) so this doesn't need dynamic allocation, and avoids heap
      fragmentation risk on a device meant to run indefinitely.
      `tmp` is now a bounds-checked `char[GPS_FIELD_MAX_LEN]` (32 bytes, sized for a single
      NMEA field rather than a whole sentence). `msg` turned out to be dead code -- written to
      but never read anywhere -- so it was deleted rather than given a buffer. The `String`-
      taking setters (`time`/`day`/`month`/`year`/`rmctime`/`rmcdate`) now take `const char *`
      and use `atoi`/`atof` instead of `String::toInt`/`toFloat`. Verified both `GPS_USES_RMC`
      and `GPS_USES_RMC`+`GPS_GGA_IS_FIRST` compile cleanly in addition to the default path.
- [x] Mark `InputCapture`'s ISR-written fields (`lastCount`, `lastMillis`, `captures` in
      InputCapture.cpp:29) `volatile`, or add a comment noting reliance on single-word atomicity
      on this ARM core.

## Design / future work

- [ ] No leap-second handling (`TODO` already in NTPServer.cpp:58) — relevant if this ever
      needs to be a stratum-1 source other systems depend on for a long-running deployment.
- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.
