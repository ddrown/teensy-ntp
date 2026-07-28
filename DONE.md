# Done

Completed items moved out of TODO.md, in their original order, with the implementation notes
that were added as each one was closed out. See `TODO.md` for what's still open.

## GPS lock lost / PPS stopped

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

- [x] **Mixed timestamp domains make this easy to get wrong twice.** The codebase compares/
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
      gap. Wired into `ClockHoldover`'s staleness and per-poll-tick duration accumulation and, as
      of 2026-07-14, the existing `ppsToGPS` lag check site too (see above) -- no known
      remaining raw *millis()-domain* duration subtraction outside `Elapsed.h` itself and
      `webcontent`'s cosmetic display value. Not yet applied to the NTP-seconds domain:
      `NTPClients::expireClients()` (NTPClients.cpp:55) still computes `expire_time = sec - 4096`
      via raw subtraction on NTP seconds, not milliseconds -- but this is a much lower-priority
      loose end than it first looks: NTP's 32-bit seconds field wraps roughly every
      2^32/(365.2425*86400) ≈ 136.19 years (the well-known "NTP rolls over in 2036" period), not
      days or seconds like `millis()`/the 1588 counter, so actually hitting that wraparound would
      require this specific device to run continuously for many decades -- not a realistic risk
      for this deployment. Left untouched by this round; noted for completeness, not urgency.
      Type safety added 2026-07-15 for the *other* mixed-domain risk in this area -- not
      wraparound, but the wire-format-vs-TAI-like NTP-seconds confusion introduced when
      `DateTime::ntptime()` became leap-second-aware (see "Leap second handling" below).
      `NtpTimestamp.h` adds `WireNtpTime`/`TaiNtpTime` (tagged `uint32_t` wrappers, explicit
      single-arg constructors, no implicit conversion between them, no arithmetic/comparison
      operators -- callers do that via the public `.v` field) so passing one domain where the
      other is expected is now a compile error instead of a silently-wrong number, threaded
      through `DateTime`/`LeapSeconds`/`NTPClock`/`ClockPID`/`ClockDiscipline`/`NTPServer`/
      `NTPClients`/`WebContent`. Also added `Ntp64` (`NtpTimestamp.h`), a named replacement for
      the `union{uint32_t parts[2]; uint64_t whole;}` pattern previously duplicated in
      `NTPClock.h` and `NTPServer.cpp`'s `recv()`, with `.seconds()`/`.fractional()`/
      `setSeconds()`/`setFractional()` accessors instead of raw `parts[TS_POS_S]` indexing.
      This work caught two real bugs along the way (not hypothetical -- both were live in the
      committed code): `NTPClients::expireClients()` (the very function referenced above)
      compared a TAI-like `localClock.getTime()` value directly against `WireNtpTime`-domain
      stored `rx_s` timestamps with no conversion, expiring clients up to ~37s early/late;
      and `WebContent`'s `gpstime` display value (`index_js.h`'s `(json.gpstime-2208988800)*1000`)
      was rendering the TAI-like value directly, showing a UTC time ~37s ahead of real GPS time.
      Both fixed by converting via `taiToWireNtp()` at the point the value is stored/displayed.
      All 69 host-side tests still pass; `NTPServer.cpp`/`NTPClients.cpp` remain uncompilable
      standalone in this environment (missing real lwIP headers, confirmed as a pre-existing
      limitation predating this change, not a regression from it) so those two files' changes
      couldn't get compiler verification here -- reviewed by hand instead.
      Last loose end closed 2026-07-19: `NTPClients::expireClients()`'s raw
      `expire_time = nowWire - 4096` / `rx_s.v < expire_time.v` subtraction (flagged above) is
      now done via `elapsedWithin(nowWire.v, rx_s.v, 4096, &age)` (`Elapsed.h`, already used
      elsewhere in this file for exactly this pattern) -- a client is kept only if its last
      `rx_s` is within `[0, 4096]` seconds of now; anything else (too old, or `nowWire` somehow
      behind `rx_s`) gets expired. This fixes the same wraparound bug class as "NTP timestamp
      era rollover (Y2036)" above, just one domain over (32-bit wire-format NTP seconds instead
      of `DateTime`'s calendar encoding): right at that ~136-year wrap, a client that registered
      just before it has a large `rx_s.v` while `nowWire` is a small value just past it, so the
      old raw-subtraction `expire_time` underflowed to a huge value and incorrectly expired a
      genuinely fresh client. `NTPClients.cpp` turned out to already have host-side test
      coverage (`test/test-NTPClients.cpp`, contrary to this file's and `CLAUDE.md`'s older
      "untested, needs real lwIP" note -- it links against `ArduinoFake`'s `ip4_addr_t`/etc.
      stand-ins fine), so this got real compiler + test verification, not just a by-hand review:
      added `test_expireClients_survives_wire_domain_wrap` (`wireNow=5`, `wireRx=0xFFFFFFFB`,
      both below the first LeapSeconds table entry so TAI==wire with no leap-offset conversion
      involved, isolating the raw arithmetic) alongside the two pre-existing `expireClients()`
      tests, which still pass unchanged since `4096`/`4090` fresh-vs-stale boundary is preserved
      exactly (`elapsedWithin(..., 4096, ...)` fresh-window matches the old `< nowWire - 4096`
      expiry threshold 1:1). `test/Makefile`'s `test-NTPClients` link rule gained `Elapsed.o`.
      All 108 host-side tests (11 binaries) pass.
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

## Leap second handling

- [x] **Design: compiled-in leap-second table, step-only (no smear), no web override.**
      Discussed 2026-07-14. `response->leap` is hardcoded to `NTP_LEAP_NONE` today
      (NTPServer.cpp:58, `TODO: no leap second support`) — relevant if this ever needs to be
      a stratum-1 source other systems depend on for a long-running deployment. NMEA has no
      vendor-neutral way to signal an upcoming leap second (only proprietary extensions like
      u-blox's `UBX-NAV-TIMELS`), and depending on one locks the firmware to a single GPS
      vendor. Rather than get this from the GPS receiver at all, follow the approach
      ntpd/chrony/ntpsec already use: consult a known table of leap-second dates, the same
      idea as IERS/NIST's published `leap-seconds.list`.
      - A small compiled-in table (new `LeapSeconds.h/.cpp`), the same "pre-generated static
        data" pattern already used for `index_html.h`/`index_js.h` — an ascending array of
        NTP timestamps marking each known leap-second insertion (every leap second since
        1972 has been an insertion; no deletion has ever happened, but the table entry should
        still carry a type field for completeness, since `NTP_LEAP_59S` is already defined in
        NTPServer.h and unused). Refreshed on firmware rebuilds as IERS Bulletin C announces
        new ones (typically ~6 months' notice).
        Table built 2026-07-15: `LeapSeconds.h/.cpp`, format mirrors the real
        `leap-seconds.list` directly (NTP timestamp the new offset takes effect, paired with
        the *cumulative* TAI-UTC offset rather than a delta, so no summing is needed at lookup
        time) — all 28 real entries from 1972-01-01 through the current 37s baseline
        (2017-01-01), hand-computed and cross-checked against `DateTime`'s independently
        implemented calendar math in `test/test-LeapSeconds.cpp` (only checkable for the
        2017 entry -- `date2days()` is documented valid for 2001..2178 only, DateTime.cpp:11,
        so the 1972 baseline entry is outside what `DateTime` itself can represent). Two pure
        lookups: `leapSecondOffsetAt(ntpTime)` (cumulative offset in effect, for the
        `DateTime` monotonic-conversion fix below) and `leapSecondPendingToday(ntpTime, &type)`
        (for `NTPServer::recv()`'s `leap` field, below) — both scan the table from the end
        since the entry in effect is always the *last* one whose `effectiveNtpTime` has passed
        (caught in review: an earlier forward-scan-with-`break` draft was accidentally correct
        but indirect enough to be worth simplifying rather than trusting). 10 tests cover: the
        DateTime cross-check, before-the-first-entry defaulting to offset 0, the exact-instant
        boundary (offset flips *at* `effectiveNtpTime`, not before), one second on each side of
        that boundary, holding the last known offset indefinitely past the newest entry, and
        the pending-day window's own boundaries (starts fresh at day-start, still pending one
        second before the instant, no longer pending exactly at it). Not yet wired into
        `NTPServer`/`DateTime` -- this is just the standalone, tested lookup module.
      - Considered and dropped a runtime web-API override for the gap between "IERS
        announces one" and "next firmware rebuild/reflash": leap seconds are announced ~6
        months ahead and happen at most once a year (and are being phased out by 2035 per
        the ITU decision anyway), so "rebuild before the next one" is a low-frequency,
        low-stakes maintenance task, not an operational burden — not worth a new
        unauthenticated web endpoint that can alter NTP protocol signaling, or the
        flash/EEPROM persistence work needed to survive a reboot (no persistent storage
        exists anywhere in this codebase today; `settings.h` is compile-time only). If this
        ever becomes worth revisiting, reconsider then rather than building it speculatively
        now.
      - `NTPServer::recv()`'s `response->leap = NTP_LEAP_NONE` becomes a lookup against the
        compiled table: `NTP_LEAP_61S` once within the current UTC day of a scheduled
        insertion, `NTP_LEAP_NONE` otherwise.
        Wired in 2026-07-15: `response->leap` now calls `leapSecondPendingToday(reftime,
        &pendingType)` (`NTPServer.cpp`), mapping `LEAP_DELETE` to `NTP_LEAP_59S` and
        `LEAP_INSERT` (or no pending entry) to `NTP_LEAP_61S`/`NTP_LEAP_NONE` respectively.
        `reftime` is already in the right domain for this -- it's set from
        `r.gpstime`/`gps.GPSnow().ntptime()` (teensy-ntp.ino), the same NTP-seconds-since-1900
        convention the table is keyed on -- so no conversion was needed. `NTPServer.cpp` has
        no host-side unit tests (it depends directly on lwIP, per CLAUDE.md), so this couldn't
        get direct test coverage; `leapSecondPendingToday()` itself is already covered by
        `test/test-LeapSeconds.cpp`, and the wiring here is a two-line lookup-and-map with no
        independent logic of its own.
      - Step, not smear: at the moment the leap second occurs, step the local clock
        representation by the extra second rather than spreading the correction over the
        surrounding day. Simpler to implement, and matches what a stratum-1 GPS-disciplined
        source is expected to do — leap smearing is a technique secondary
        servers/cloud providers use to hide leap seconds from clients that can't handle them,
        not something a primary reference clock does.
      - **Resolved 2026-07-14, `DateTime` does need a real fix, and it's this one:**
        `DateTime::ntptime()`/`unixtime()` (`time2long()`, DateTime.cpp:31, already flagged
        there as ignoring leap seconds) do plain linear `days*86400 + h*3600 + m*60 + s`
        arithmetic. If GPS ever reports a literal `23:59:60` for an inserted leap second,
        that computes to the *exact same* value as the following `00:00:00` — confirmed with
        `test_leap_second_ntptime_collides_with_next_day` in `test/test-DateTime.cpp` (real
        2016-12-31 leap second date; both sides equal `536544000` — no bounds-checking on
        `second` rejects or clamps 60, `test_second_60_does_not_corrupt_fields` confirms the
        other fields at least survive intact). Two distinct PPS pulses a full second apart
        would alias to the same `realSecond` value fed into `ClockDiscipline`/`ClockPID`,
        which reads as zero elapsed real-time for one PPS interval's worth of hardware-counter
        ticks and corrupts the Theil-Sen drift-rate regression for as long as that sample
        stays in `ClockPID`'s 16-deep window (~48s at the current resolve cadence). A deletion
        (never yet happened, but `NTP_LEAP_59S` exists) would produce the opposite artifact —
        an apparent +2s jump.
        Fix: make `ntptime()`/`unixtime()` monotonic ("TAI-like", though it doesn't need to be
        literally standards-defined TAI — just internally self-consistent) by adding a
        `leapSecondsSoFar(date)` cumulative-offset term, sourced from the same
        `LeapSeconds` table, to the linear day/time arithmetic. `ClockPID`/`ClockDiscipline`/
        `NTPClock` need *no* math changes for this — their regression/offset calculations are
        already difference-based (e.g. `realSeconds[i] - realSeconds[0]` in
        `ClockPID_c::calculate_d()`), so a constant offset cancels out and a step-change
        mid-window is exactly what they're already equipped to handle correctly once the
        input is genuinely monotonic. The one place that *does* need an explicit reverse
        conversion is `NTPServer`'s outgoing packet assembly, since the wire format is
        standards-defined UTC-seconds-since-1900 (the reason NTP has the `leap` field at all)
        — subtract the cumulative offset back out there, using the same table. Still needs
        confirming against how the specific GPS module in use actually behaves during the
        leap-second minute (some receivers emit a valid `:60`, others skip straight to `:00`
        or repeat `:59`) — the fix above assumes a receiver that does emit `:60`.
        Implemented 2026-07-15: `DateTime::ntptime()`/`unixtime()` (encode) now add
        `LeapSeconds`'s cumulative offset via a new `leapOffsetFor()` helper (`DateTime.cpp`),
        looked up in the wire-format domain via `leapSecondOffsetAt()` -- using the *prior*
        offset for a literal `second_==60` so it doesn't pick up the boundary's new offset a
        second early. `DateTime::time(uint32_t t)` (decode, the `DateTime(uint32_t)`
        constructor) is the mirror image: added `LeapSeconds.h`'s new
        `leapSecondOffsetAtTai(taiTime, &isLeapInstant)`, a reverse-domain lookup that scans
        the same table using each entry's *TAI-domain* boundary (`effectiveNtpTime +
        cumulativeOffset`, exact, not an approximation) so it can subtract the right offset
        and detect the literal leap-second instant precisely, decoding it back to `second_ ==
        60` instead of aliasing into the next day. `DateTime(uint32_t)` was unused anywhere in
        production code before this (only tests exercised it), so the domain-contract change
        (raw wire timestamp -> TAI-like) doesn't affect any existing call site.
        `taiToWireNtp(taiTime)` (also new, `LeapSeconds.h/.cpp`) wraps the reverse lookup for
        callers that just need the wire-format value back, not the leap-instant flag --
        `NTPServer::recv()` uses it for every timestamp it writes onto the wire or compares
        against a client-echoed value (`ref_time`, `recv_time`, `trans_time`, and the
        `clientList` interleaved-mode bookkeeping in `addTxTimestamp()`/`addRx()`), since
        `localClock_`/`reftime` are now TAI-like too (`ClockPID`/`ClockDiscipline`/`NTPClock`
        needed no code changes, per the design above -- they inherited the TAI-like domain
        automatically just by being fed `ntptime()`'s new output, and stayed correct because
        their math is difference-based). This was the one part of this fix with real blast
        radius: every served packet's timestamp needed the reverse conversion, not just ones
        near a leap second, since `NTPClock`'s internal anchor itself is now TAI-like. Test
        coverage: `test/test-DateTime.cpp` covers the encode/decode round trip through the
        2016-12-31 leap second (`test_leap_second_ntptime_is_monotonic_not_aliased`,
        `test_leap_second_ntptime_roundtrips_through_decode`); `test/test-LeapSeconds.cpp`
        covers `leapSecondOffsetAtTai()`/`taiToWireNtp()` directly, including that the leap
        instant and the following day's `:00` still collide on the *wire* (expected -- that's
        the wire format's own inherent limitation, not something this conversion should hide).
        `NTPServer.cpp` itself has no host-side tests (lwIP dependency, per CLAUDE.md), so the
        packet-assembly wiring couldn't get direct coverage.

      **Real GPS receiver behavior during a leap second, per time-nuts mailing-list reports
      (2026-07-17): the `:60` fix above covers only one of at least three observed behaviors.**
      Different modules have been observed to: emit a literal `23:59:60` (handled by the fix
      above); repeat `23:59:59` (send two consecutive fixes/PPS pulses both claiming
      `23:59:59`); or, on at least one module, something stranger still -- repeat an *earlier*
      second (`23:59:57`) rather than the one immediately before the boundary, apparently a
      vendor bug rather than a documented behavior. A fourth theoretical option -- skip `:60`
      entirely and jump straight from `23:59:59` to `00:00:00` -- isn't in that report but is
      worth naming too, since it's the one case that's already harmless (see below).

      The `:59`-duplicate case is a real, currently-unhandled gap, and it can't be closed inside
      `DateTime` the way the `:60` case was: `DateTime` is a stateless calendar<->timestamp
      converter, so two consecutive PPS pulses that both report calendar time `23:59:59` produce
      *identical* `ntptime()` output -- the exact zero-elapsed-time aliasing bug this whole area
      of work exists to fix, just triggered by a different receiver behavior than the one the
      current fix targets. Fixing it requires state (remembering the previous accepted sample),
      which only exists one layer up, where consecutive samples actually get compared.

      Rather than special-case each vendor's specific quirk (which the `:57` report suggests is
      a losing game -- there's no clean pattern to a vendor bug), the more robust fix is a
      general monotonicity guard where GPS samples are consumed: `ClockDiscipline::process()`
      (already the class that owns "should this sample be trusted," via the median-of-3/
      bootstrap logic -- see above, "extracting the logic out of the ino file"). This
      composes cleanly with the existing `:60` fix rather than conflicting with it: a receiver
      that correctly emits `:60` never triggers this guard at all, since `DateTime` already gives
      it a genuinely distinct, monotonically-increasing `gpstime` every PPS pulse -- the guard is
      purely additional protection for the receivers that don't.
      - New state: `ClockDiscipline` remembers the previous *accepted* sample's `gpstime`
        (`TaiNtpTime lastGpstime_` or similar), updated on every accepted sample, meaningless
        before the first one (mirrors the existing `settime_` bootstrap flag).
      - New first check in `process()`, before today's median-of-3/bootstrap branching (so a
        bad sample never pollutes the median buffer, same principle as the existing
        `elapsedWithin()` PPS/GPS lag check rejecting *before* `discipline.process()` is even
        called): classify the incoming `gpstime` against `lastGpstime_`.
        - `gpstime > lastGpstime_`: normal, proceed exactly as today.
        - `gpstime == lastGpstime_` **and** `leapSecondPendingToday(taiToWireNtp(gpstime))` is
          true: this is the leap-second duplicate -- step the sample forward by one full second
          (synthesize `gpstime + 1`) before feeding it to the median buffer/PID, matching what a
          `:60`-emitting receiver would have produced natively. Store the *stepped* value as the
          new `lastGpstime_`, not the original, so the next comparison (whatever the receiver
          does next) is against a correctly-advanced baseline.
        - `gpstime == lastGpstime_` but **not** within a pending-leap-second day: a spurious
          duplicate fix unrelated to a leap second (a GPS glitch, a repeated NMEA sentence) --
          reject it the same way an out-of-tolerance lag sample already gets rejected today (no
          PID sample, no reftime update, `lastGpstime_` unchanged).
        - `gpstime < lastGpstime_`: a backwards jump -- never valid, leap second or not (this is
          exactly the `:57`-style vendor-bug case) -- always reject, regardless of
          `leapSecondPendingToday()`. A leap second only ever produces a *stall* (same value
          again), never something that looks like it went backwards.
      - `DisciplineResult` needs a way to distinguish "rejected" and "accepted via leap-second
        correction" from a normal update, so `updateTime()` (teensy-ntp.ino) can log it
        distinctly -- matching the existing `"S "`/`"LAG "` diagnostic-prefix convention already
        used there for clock-set/lag-rejected messages.
      - The fourth, unreported-but-plausible case -- skipping `:60` and silently jumping straight
        to `00:00:00` -- needs no special handling under this design: no duplicate occurs (each
        PPS pulse still gets a distinct, correctly-increasing `gpstime`, since the receiver just
        never reports `:60` or repeats anything), so `ClockDiscipline`'s regression is never fed
        a corrupt zero-elapsed sample. The served time is transiently 1s off from true UTC right
        after the leap second (the receiver silently ate it), but that's a discrete offset the
        PID's existing P/I terms already know how to correct over subsequent samples, same as
        recovering from any other momentary offset -- not a regression-corrupting bug like the
        other three.

      **Implemented 2026-07-18.** One refinement made during implementation, worth calling out:
      the design above said to key the leap-stall check off `leapSecondPendingToday()`, which
      flags the *whole UTC day* a leap second is scheduled (the right behavior for the wire LI
      field, which has to warn clients a day in advance) -- reusing it here would have been a
      real bug, not just an imprecision. A duplicate `gpstime` from an unrelated GPS glitch at,
      say, noon on the leap day would have been misclassified as the leap-second stall and
      *stepped forward by one second before being fed to the regression* -- reintroducing
      exactly the spurious +1s-jump corruption this whole fix exists to prevent, just gated
      behind a rarer (1-day-a-year-ish) coincidence instead of every leap second. Added a new,
      narrower lookup instead: `leapSecondStallSecond(WireNtpTime, LeapSecondType*)`
      (`LeapSeconds.h/.cpp`) -- true only for the exact instant a stall/duplicate can genuinely
      happen, the last regular second before a boundary (`effectiveNtpTime - 1`, i.e. 23:59:59
      the day of an insertion), not the whole day. `ClockDiscipline::process()` calls this one,
      not `leapSecondPendingToday()`.

      `ClockDiscipline.h` gained `lastGpstime_` (private state, initialized on both the
      clock-set path and the normal-accept path) and `DisciplineResult` gained `rejected`/
      `leapSecondCorrected` bools, matching the design above exactly otherwise: `gpstime <
      lastGpstime_` rejects unconditionally; `gpstime == lastGpstime_` checks
      `leapSecondStallSecond(taiToWireNtp(gpstime), ...)` and either steps forward by 1 and
      accepts, or rejects. `teensy-ntp.ino`'s `updateTime()` logs both outcomes distinctly
      (`"D "` for rejected, `"L "` for leap-corrected), matching the existing `"S "`/`"LAG "`
      convention, and returns early without touching `holdover`'s dispersion/reftime paths on
      rejection (the `noteSampleReceived()` "GPS is alive" signal still fires either way, same
      as before -- a rejected sample means bad *data*, not silence).

      Extending `test-ClockDiscipline.cpp` surfaced one more thing the original design didn't
      anticipate: two existing tests (`test_full_pid_selects_median_ascending_order`/
      `_descending_order`) constructed out-of-arrival-order *samples* by feeding `gpstime`
      itself out of order (with `pps` held fixed) -- which the new monotonicity guard now
      correctly rejects, since that's no longer a realistic scenario once gpstime must advance
      monotonically. Fixed by keeping `gpstime` strictly increasing (1 real second per call, as
      it always is in reality) and instead varying `pps` (hardware ticks) per call so the local
      clock's own extrapolated elapsed time produces the desired out-of-order *offset* sequence
      -- still exercises the same `median()` branches, just via the parameter that can
      legitimately vary non-monotonically (offset/drift, not true time). The specific pps/gpstime
      values were derived and cross-checked with a small script mirroring
      `NTPClock::getTime()`/`getOffset()` exactly, not hand-approximated, given how easy the
      64-bit fixed-point carry arithmetic is to get subtly wrong by hand. Two other existing
      tests (`test_resolve_updates_local_clock`, `test_buffering_calls_do_not_touch_local_clock`)
      previously reused one identical `gpstime` across 2-3 consecutive calls purely as a
      simplification (their assertions never depended on the value itself); switched to
      distinct increasing values with an updated (still hand-verified) expected median.
      New tests: a duplicate outside any leap window is rejected (`pid` sample count and
      `lastGpstime_` both unchanged); the exact 2016-12-31 23:59:59 TAI value (already
      cross-checked in `test-LeapSeconds.cpp`) repeated back-to-back is accepted with `gpstime +
      1` and does reach the PID; and a backwards jump starting from that same leap-adjacent value
      is still rejected, confirming the leap-window check can't be tricked into forgiving a
      genuine backwards jump. `test-LeapSeconds.cpp` gained direct coverage of
      `leapSecondStallSecond()`, including the noon-on-leap-day case that motivated narrowing it
      in the first place. All 105 host-side tests pass (7 newly added -- 3 in
      `test-ClockDiscipline.cpp`, 4 in `test-LeapSeconds.cpp` -- plus the existing 98, several of
      which needed their `gpstime` values adjusted as described above without changing what they
      actually verify).

      Hardware confirmation done 2026-07-19: this device's GPS module's actual leap-second
      behavior has been confirmed. The general monotonicity guard above was built precisely so
      correctness doesn't depend on knowing this in advance, so this was closing the loop rather
      than unblocking anything.

## Test coverage for NTPServer

- [x] **Extract `NTPServer::recv()`'s lwIP-independent logic into a testable core, rather than
      mocking lwIP's pbuf/udp_pcb API.** Raised 2026-07-15: `NTPServer.cpp`/`NTPClients.cpp` have
      no host-side tests (`CLAUDE.md`) and, confirmed today, don't even compile standalone in this
      environment -- `test/lwip/inet.h`/`test/lwip_t41.h` are near-empty stubs (just enough for
      files like `NTPClock.cpp` that only need the headers to *exist*, not files that actually
      touch `ip4_addr_t`/`struct pbuf`/`struct udp_pcb`). That meant the `WireNtpTime`/`TaiNtpTime`
      wiring through `recv()` (see git history, "add typed WireNtpTime/TaiNtpTime wrappers") could
      only be checked by hand, not by the compiler -- exactly the file where a domain mixup is
      most likely and least visible, since it's the one place real wire-format output gets
      produced. Considered mocking lwIP directly (flesh out the stub headers, maybe with
      FakeIt-style call verification on `udp_sendto()` the way `ArduinoFake` already mocks
      `Serial`/`millis()`) -- rejected as the primary approach: lwIP's real pbuf refcounting/udp_pcb
      semantics are enough surface area that a hand-rolled stub risks quietly diverging from real
      behavior, which is worse than no test (a mock that lies gives false confidence in exactly the
      kind of subtle correctness bug this area already has a track record of).

      Walking `recv()` (NTPServer.cpp) line by line, most of the *interesting* logic doesn't
      actually touch lwIP at all -- it just happens to be written inline alongside the parts that
      do:
      - Request validation (`request_buf->len < sizeof(ntp_packet)`, `request->version`,
        `request->mode`) -- pure once the three input values are extracted from the pbuf.
      - Stratum/ident/leap selection (`reftime`/`dispersion` -> stratum 1 vs 16, `NTP_LEAP_UNSYNC`
        vs the `leapSecondPendingToday()` lookup) -- pure function of `(TaiNtpTime reftime,
        uint32_t dispersion)`. This is the single most valuable piece to get under test: it's the
        leap-indicator logic from the "Leap second handling" work above, and the exact kind of
        thing a hand-review can miss.
      - Poll clamping (`min(request->poll, 12)`) -- trivial, but free to include.
      - The RX/TX timestamp computation (`Ntp64` pack + latency correction + `taiToWireNtp()`) --
        already `pbuf`/`udp_pcb`-free today (operates on `TaiNtpTime`/`Ntp64` only), just needs
        pulling out of `recv()`'s body; doing so also removes the near-duplicate RX/TX logic
        (same three steps, different constants) into one shared helper.
      - What *can't* be extracted without more work: the interleaved-vs-basic-mode decision
        depends on `NTPClients::findClient()`, which needs `CLIENT_ADDR_T` (a real `ip4_addr_t`/
        `ip6_addr_t`) for address matching.

      Proposed shape: a new `NTPResponseBuilder.h/.cpp` (or similar; mirrors the
      `ClockDiscipline`/`ClockHoldover` extraction pattern already used this session) exposing
      small, separately-testable pure functions/methods for the pieces above, taking and returning
      plain values (`TaiNtpTime`/`WireNtpTime`/`uint32_t`/a small result struct) -- no `pbuf`,
      `udp_pcb`, or `ip_addr_t` in any of their signatures. `NTPServer::recv()` shrinks to: extract
      fields from the pbuf, call the builder, look up the interleaved client, write the result
      into the response packet, call `udp_sendto()`. That remaining shell is simple enough
      (straight-line field copying) that it's much lower-risk to leave hand-reviewed than the
      logic being pulled out of it.

      Separate, smaller, complementary opportunity noticed along the way, **done 2026-07-15**:
      `NTPClients.cpp` (`addRx`/`addTx`/`findClient`/`expireClients`) never touches
      `pbuf`/`udp_pcb`/`enet_*` at all -- its only lwIP dependency is
      `CLIENT_ADDR_T`/`CLIENT_ADDR_CMP`/`CLIENT_ADDR_SET` (i.e. `ip4_addr_t` and address
      compare/copy). Fleshed out *just* that slice of the stub headers instead of the full
      udp/pbuf machinery: `test/lwip/inet.h` (previously empty) now defines a real `ip4_addr_t`
      (`{ uint32_t addr; }`) plus `ip4_addr_set`/`ip4_addr_cmp` as `static inline` functions
      mirroring real lwIP's semantics (value copy with NULL-source-means-zero; equality compare) --
      `LWIP_IPV6` is never defined in this test build (it comes from the real `lwipopts.h`, part of
      the external `teensy41_ethernet` library this repo doesn't vendor), so `NTPClients.h`'s
      `#if LWIP_IPV6` always takes the ip4 branch here, same as every other file already built
      without that library. `NTPClients.cpp` now compiles and links standalone in this environment
      for the first time. New `test/test-NTPClients.cpp` (11 tests): `findClient()`'s three-way
      match (address, rx seconds, rx fractional) including each independently-wrong case;
      `addRx()` reusing an existing address's slot rather than creating a duplicate; `addTx()`
      requiring a prior matching `addRx()` and matching port; and `expireClients()`, including a
      **regression test that specifically exercises the TAI/wire domain bug fixed alongside the
      typed wrappers** -- a client refreshed only 6 seconds ago (in the correct, converted domain)
      that would have been wrongly expired as ~4127s old if `localClock`'s TAI-like "now" were
      compared against the wire-domain `rx_s` without the `taiToWireNtp()` conversion. Needed one
      other fix along the way: `NTPClients::expireClients()` reaches for the real global
      `localClock` directly rather than taking it by constructor injection like
      `ClockDiscipline`/`ClockHoldover`/`NTPServer` do -- unlike those, `NTPClients`'s global
      instance (`clientList`) is defined only in `teensy-ntp.ino`, not in `NTPClients.cpp` itself
      (`ClockPID` is the one existing singleton that *does* self-define in its own `.cpp`), so the
      test binary defines its own `NTPClock localClock;` to satisfy the link, the same role
      `teensy-ntp.ino` normally plays. Left as-is rather than refactoring `expireClients()` to take
      DI -- out of scope for "add test coverage," and would touch the one already-untestable-without-help
      call site's public signature for no test-writing benefit beyond what defining the global
      already gets for free.
      Makefile rule: `test-NTPClients: test-NTPClients.o NTPClients.o NTPClock.o LeapSeconds.o
      $(LIBS)`. All 80 host-side tests (11 new + 69 existing) pass.

      **`recv()` extraction done 2026-07-15.** New `NTPResponseFields.h/.cpp`: pure,
      lwIP-independent functions for every piece identified above --
      `ntpRequestLengthIsValid()`/`ntpRequestVersionAndModeAreValid()` (split in two, not one
      combined check, to preserve the original safety-critical order: the length must be
      confirmed before it's safe to read `request->version`/`request->mode` off the packet at
      all), `selectNTPResponseHeader()` (the stratum/ident/leap logic -- the piece this was
      mainly about), `clampNTPPoll()`, and `ntpWireTimestampFromTai()`/`ntpWireTimestampFromWire()`
      (the RX/TX conversion, now shared instead of duplicated -- two variants because the two
      real call sites genuinely differ: RX and basic-mode TX start from a TAI-like
      `NTPClock::getTime()` sample and need the domain conversion, interleaved-mode TX starts
      from an already-wire-format stored `tx_s` and must *not* convert again). Also split
      `NTPServer.h`'s packet struct/protocol constants into a new `NTPPacket.h` -- `ntp_packet`
      and `NTP_MODE_*`/`NTP_LEAP_*`/etc. never needed lwIP themselves, only `NTPServer::recv()`'s
      *signature* does (`struct pbuf`/`ip_addr_t`), so `NTPResponseFields.h` can depend on the
      packet layout without pulling lwIP headers into a file that has nothing to do with lwIP.
      `NTPServer::recv()` shrunk from ~120 lines to the intended shell: pbuf field extraction,
      calls into the new pure functions, interleaved-client lookup, write results into the
      response packet, `udp_sendto()` -- no more inline `Ntp64` construction or duplicated
      RX/TX correction arithmetic.
      New `test/test-NTPResponseFields.cpp` (18 tests) covers the full plan sketched above:
      request length/version/mode validity (each independently); unsynced-via-zero-reftime,
      unsynced-via-over-threshold-dispersion, and the exact-threshold boundary; synced with no
      leap pending and with a `LEAP_INSERT` pending (reusing the same 2016-12-31 fixture
      `test-LeapSeconds.cpp` uses); poll clamping under/at/over the limit; and the wire-timestamp
      conversion for a normal instant, a fractional-carry case (verified against hand-computed
      carry arithmetic), the literal leap-second instant (reusing the
      `test_tai_to_wire_ntp_leap_instant_and_next_day_collide_on_wire` fixture), and the
      already-wire-format (`ntpWireTimestampFromWire`) path used by interleaved mode.
      Makefile rule: `test-NTPResponseFields: test-NTPResponseFields.o NTPResponseFields.o
      LeapSeconds.o $(LIBS)`. All 98 host-side tests (18 new + 80 existing) pass.
      `NTPServer.cpp` itself still can't compile standalone in this environment -- confirmed via
      the same before/after error-count comparison used for the `WireNtpTime` work (39 errors
      pre-extraction, 25 post-extraction, all of them the same pre-existing `ip_addr_t`/`struct
      pbuf`/`struct udp_pcb`/`htonl`-family/`enet_*`/`pbuf_*`/`udp_*` gaps, nothing new) -- but
      the shell left behind is now small and straight-line enough that hand review of it carries
      much less risk than hand-reviewing the logic that used to live inline in it.

## NTP timestamp era rollover (Y2036)

- [x] **Design: compare firmware-internal timestamps in seconds-since-2000, not wire-format
      `ntptime()`, to sidestep the 32-bit NTP wrap (2036-02-07) instead of working around it.**
      Raised 2026-07-15 while checking whether `LeapSeconds` needs to handle the NTP-seconds
      wrap; corrected the same day after review caught a wrong initial read of `DateTime`;
      settled on this specific fix the same day after further discussion.

      `DateTime`'s `SECONDS_FROM_1900_TO_2000` rebasing (`DateTime::time(uint32_t t)`, the
      *decode* direction) is **not** a bug or an oversight -- it's a deliberate, already-correct
      era pivot. The 1900-2000 sub-range of the 32-bit space is useless for this device (it will
      never legitimately represent a pre-2000 date), so reusing that dead space to instead mean
      "post-2036-wrap" gives `time()`/`ntptime()`/`unixtime()` a genuinely self-consistent
      round-trip across a full ~136-year window: 2000-01-01 through ~2136-02-07. Verified by
      direct calculation: 2037-01-01 encodes (via unsigned overflow in `ntptime()`'s
      `t += SECONDS_FROM_1900_TO_2000`) to the wrapped raw value `28402304`, and decoding
      `28402304` back through `time()` reproduces 2037-01-01 exactly --
      `test_century_not_leap` in `test/test-DateTime.cpp` already exercises a round-trip inside
      this window (year 2100) and passes. `GPSDateTime::GPSnow()` also has no ambiguity in *what
      date it is* going in, since it's built from GPS's own real absolute calendar fields
      (year/month/day/h/m/s parsed off NMEA -- at least via ZDA's 4-digit year; RMC's 2-digit
      year hardcodes a `+2000` century assumption in `rmcdate()`, a separate Y2100-class issue
      not raised here). So: no fix needed in `DateTime`'s encode/decode math itself.

      What *isn't* covered by that per-value round-trip correctness: raw numeric comparisons
      **between two independently-produced timestamps** that straddle the wrap. Two correctly
      round-tripping values can still compare backwards, because "chronologically later" doesn't
      mean "numerically larger" once one side has wrapped and the other hasn't. Concrete case
      that would actually break this device: `gps_serial_poll()` does
      `if(gpstime < compileTime) { ... reject as "gps clock bad" ... }` (teensy-ntp.ino).
      `compileTime` is computed once at boot from `__DATE__`/`__TIME__`, necessarily before the
      wrap (a large raw value); `gpstime` for a real date after 2036-02-07 correctly wraps (per
      the above) to a *small* raw value. `gpstime < compileTime` then evaluates true, and the
      device would permanently reject every real GPS fix as "bad" from that moment on, with no
      recovery short of a firmware rebuild (which would reset `compileTime` forward and mask the
      problem again only until the *next* wrap-adjacent comparison, rather than fixing it).

      `LeapSeconds`-specific consequence, currently latent: `leapSecondOffsetAt()`/
      `leapSecondPendingToday()` assume `leapSeconds[]` is sorted in ascending order, which
      today also happens to be chronological order, since all 28 entries (1972-2017) sit in the
      "direct" (pre-wrap) sub-range. A hypothetical future entry dated after 2036-02-07 would,
      under the same `ntptime()` convention, encode to a numerically *small* raw value --
      it would need to sort at the *front* of the array to stay numerically ascending even
      though it's the chronologically newest entry, or the lookup logic would need to change to
      not assume ascending-numeric-order == chronological-order. Moot today (nothing added since
      2016, and leap seconds are being phased out by 2035 per the ITU decision), but worth a
      comment for whoever eventually touches this table again near that boundary.

      Fix scope: not `DateTime`'s encode/decode functions (already correct), and not a dedicated
      wraparound-aware comparison helper either -- the `gpstime`/`compileTime` values being
      compared never leave the firmware (unlike `ntptime()`'s output, which has to match NTP's
      wire format to be useful to clients), so there's no need to make the comparison
      wraparound-*aware*; it's simpler to pick a representation that doesn't wrap within any
      realistic device lifetime and let plain `<` work.
      - Implement `DateTime::secondstime()` -- declared in DateTime.h ("32-bit times as seconds
        since 1/1/2000") but never defined or called anywhere in this codebase today. As a
        plain 32-bit counter from a 2000 epoch (no NTP-wire-format constraint forcing it to
        alias 2036-2136 onto 1900-2000 the way `ntptime()` deliberately does), it's strictly
        monotonic with real time all the way out to ~2136 -- comfortably beyond any realistic
        firmware/hardware lifetime, so no wraparound handling is needed for it at all within
        that window.
      - Considered a 64-bit `ntptime` instead (matches NTPv4's actual 64-bit "NTP Date Format",
        practically never wraps -- 2^64 seconds). Rejected as disproportionate here: it either
        widens `DateTime`'s core representation more invasively than this needs, or requires
        maintaining a second 64-bit computation path alongside the 32-bit wire-format one that
        has to stay in sync with it (including whenever the leap-second monotonic-conversion fix
        above lands) -- all to buy safety margin past 2136 that this device will never spend.
      - Scope stays narrow: only the `gpstime < compileTime` sanity check
        (`gps_serial_poll()`/teensy-ntp.ino) needs this. `ClockPID`/`NTPClock`/`NTPServer` all
        still need real NTP-wire `ntptime()` values downstream, so `secondstime()` gets computed
        alongside `ntptime()` just for that one comparison, not threaded through the rest of the
        pipeline.
      - Separately: a note (not urgent, given the ITU phase-out) for `LeapSeconds` to revisit
        its ascending-order assumption if a table entry is ever added for a date past
        2036-02-07 -- unaffected by this fix, since `leapSeconds[]`'s `effectiveNtpTime` values
        are deliberately in the wire-format `ntptime()` domain, not this new internal one.

      **Implemented 2026-07-19.** One refinement found during implementation: `DateTime.h`
      had declared `long secondstime() const;` (signed) -- but "monotonic out to ~2136" is only
      true for an *unsigned* 32-bit counter (2^32s ≈ 136 years from 2000); a signed `long`
      overflows negative after ~68 years (~2068). Since `secondstime()` was declared but never
      implemented or called anywhere (confirmed via grep, so changing it broke nothing), changed
      it to `uint32_t secondstime() const;` so the implementation actually matches the documented
      window, rather than silently shipping a ~68-year one under a "~136-year" comment. (68 years
      would likely have been fine too -- comfortably beyond any realistic device lifetime -- this
      was a doc/intent-consistency fix, not a functional necessity.)
      `DateTime::secondstime()` (`DateTime.cpp`) is `date2days(year_, month_, day_)` run through
      the existing `time2long()` with no epoch-offset arithmetic at all, since `date2days()` is
      already seconds-since-2000-01-01 by construction. Deliberately has **no `LeapSeconds`
      involvement**, unlike `ntptime()`/`unixtime()`: its only consumer is the coarse
      `gpstime`/`compileTime` sanity check, where real-world compares differ by months/years, so
      leap-second precision doesn't matter and isn't worth the dependency.
      `teensy-ntp.ino` gained a new `uint32_t compileSecondsTime` global, computed alongside
      `compileTime` in `setup()` (including the same "compile timezone up to 14 hours ahead"
      adjustment `compileTime` already gets). `gps_serial_poll()` now captures the `DateTime`
      from `gps.GPSnow()` once (rather than chaining straight to `.ntptime()`) so both
      `.secondstime()` (for the comparison) and `.ntptime()` (still fed to `updateTime()`
      unchanged) come off the same instance; the `gpstime < compileTime` check itself now
      compares `secondstime()` values instead of the wire-wrapping `ntptime()` ones. Scope stayed
      exactly as narrow as planned: `ClockPID`/`NTPClock`/`NTPServer` are untouched, since nothing
      about their real wire-format `ntptime()` values changed.
      Test coverage (`test/test-DateTime.cpp`): a basic epoch sanity check (2001-01-01 is
      31622400s after the 2000-01-01 epoch, since 2000 was a leap year); and the test that's
      actually the point of this fix -- `DateTime(2036,1,1,...)` vs `DateTime(2036,3,1,...)`
      confirmed (via a script, not hand-approximated) to encode to `ntptime()` values of
      `4291747200` and `1963904` respectively, i.e. the *later* date compares numerically
      *smaller* once wrapped, while `secondstime()` on the same two dates stays correctly
      ordered. All 107 host-side tests pass (2 new + 105 existing, unaffected).

## README

Rewrote README.md per the 2026-07-20 review, in the same priority order. One item -- whether to
add a `LICENSE` file -- stayed in TODO.md since it needs the repo owner's decision, not just
writing.

- [x] **Intro paragraph.** What this is and why a Teensy 4.1 + hardware 1588 timer instead of the
      more common Raspberry Pi + gpsd + chrony approach (dedicated timer vs. OS scheduling
      jitter, cost, power, no SD card to corrupt).
- [x] **Accuracy in practice.** Cited the actual bench result from this session's dual-Teensy
      comparison (agreement within a few hundred ns, well inside the tens-of-µs measurement
      error) alongside the theoretical `precision` field, instead of only the latter.
- [x] **GPS/PPS loss (holdover) behavior.** Documented the `HOLDOVER_STALE_MS`/`HOLDOVER_PHI_PPM`
      constants from `ClockHoldover.h`, the D-only discipline switch, and derived the actual
      stratum-16 fallback time from `growDispersion()`'s formula: dispersion grows at
      `HOLDOVER_PHI_PPM * 1e-6` per second (as a fraction of the 0x10000 = 1-second NTP
      threshold), so from a near-zero baseline it takes `1/(15e-6)` ≈ 66667s ≈ 18.5 hours of
      continuous outage before `NTPResponseFields.cpp`'s `dispersion > 0x10000` check flips the
      server to stratum 16 / LI=unsync.
- [x] **`settings.h` options.** Documented what's actually there now (`GPS_BAUD`, `GPS_SERIAL`,
      `hostnameTable[]`) rather than the TODO item's original wording -- `DHCP_HOSTNAME` was
      replaced by the MAC-keyed `hostnameTable[]` earlier this session, and `GPS_USES_RMC` turned
      out to already be gone (see `GPS.cpp`'s `decodeType()` comment: sentence type/order
      detection moved to runtime, keyed off `InputCapture::getCaptures()`, specifically to stop
      assuming a fixed sentence order -- this predates today and was never itself recorded as a
      DONE item, so noting it here). `CLAUDE.md`'s architecture section still describes the old
      `GPS_USES_RMC` macro and an out-of-date "which classes are tested" list -- both are stale
      and worth a follow-up pass, not touched here since out of scope for the README.
- [x] **Build/flash steps.** Added both the Arduino IDE + Teensyduino path and the arduino-cli
      path (PJRC's Boards Manager package index, `teensy:avr` core, manual `teensy41_ethernet`
      clone since it isn't Library-Manager-indexed) set up earlier this session, plus a pointer
      to this repo's `compile.sh`/`upload.sh`/`monitor.sh`.
- [x] **Wiring/pin table.** PPS (35), GPS TX/RX (0/1, `Serial1`), Ethernet (built-in RJ45, no
      wiring), power -- table form instead of the single PPS-pin mention.
- [x] **Troubleshooting section.** No-signal-at-all (check `GPS_BAUD` first -- `GPS.cpp` silently
      drops checksum failures with no log line, so a baud mismatch and a genuine no-sky-view
      look identical from the firmware's side; this was today's own real debugging session),
      slow-to-lock/cold-start almanac timing, no-PPS-detected (`LAG` message, `inHoldover`), and
      the `bootloader_poll()`/stray-`screen`-session "rebooting to bootloader" gotcha from
      earlier this session.
- [x] **Links to `TODO.md`/`DONE.md`/`TESTPLAN.md`.** Added under a "Testing" section alongside
      the existing `test/` mention.
- [x] **Differentiators callout.** Theil-Sen + chi-squared, compiled leap-second table + TAI-like
      domain, host-side unit tests for an Arduino sketch, IPv6 support. Also added a dedicated
      "Hardware timestamping" section after a follow-up request: PPS input capture
      (`InputCapture.cpp`) and both NTP packet RX (`lwip_t41.c`'s `p->timestamp = bdPtr->timestamp`)
      and TX (`enet_txTimestampNextPacket()`/`enet_set_tx_timestamp_callback()`) are all real MAC
      hardware timestamps, not software-timed. Explained *why* interleaved mode matters here
      specifically (not just that it's supported): the hardware TX timestamp for a given response
      isn't known until after it's physically sent, so `NTPServer.cpp` answers the first request
      to a client with a software-estimated "basic mode" transmit time, then delivers the real
      hardware-captured value on that client's *next* request via `NTPClients`' pending-timestamp
      tracking.
- [x] **`LICENSE`.** Repo owner chose MIT; added `LICENSE` and a README pointer.

## GPS_BAUD auto-detection

- [x] **Auto-detect GPS module baud rate at startup instead of a hardcoded `settings.h` value.**
      Same "one firmware image per device" problem `DHCP_HOSTNAME` used to have -- motivated by
      this session's own bench debugging (a test unit's GPS module was still at its factory 9600
      baud while the firmware assumed 115200; the mismatch produced no error, just silently zero
      satellites/no lock). `settings.h`'s `GPS_BAUD` define replaced with a `gpsBaudCandidates[]`
      array (`{115200, 9600}`), structured the same way `hostnameTable[]` already is. New
      `detectGpsBaud()` in `teensy-ntp.ino` (called from `setup()` in place of the old
      `GPS_SERIAL.begin(GPS_BAUD)`) tries each candidate in turn: `GPS_SERIAL.begin(candidate)`,
      drains whatever's buffered from the previous rate, then polls for up to `GPS_BAUD_PROBE_MS`
      (2000ms -- long enough to reliably catch at least one full per-second NMEA burst) calling
      `gps.decode()`, which already does checksum verification and only returns `true` on a
      complete, valid ZDA/RMC sentence -- reused as-is rather than reimplementing checksum
      verification. Falls back to the first candidate if nothing validates in any window, so a
      module that's just slow to start still gets a sensible rate to keep listening at. Confirmed
      safe to call before `pps.begin()` (`InputCapture`'s `getCaptures()`/`getCount()`/
      `getMillis()` are plain zero-initialized counters, no hardware register access, so probing
      doesn't touch not-yet-configured PPS capture hardware). Prints the detected rate over
      serial (`"GPS baud: <n>"`). `README.md`'s `settings.h` options table and the
      no-satellites troubleshooting bullet updated to match.

## NMEA MITM tooling (for TESTPLAN.md sections 6/7)

Built and run successfully against real hardware (see `data/mitm` and the "MITM bench session
findings" TODO.md entry for what it found). A USB-UART adapter only gives one serial connection,
so a live two-port relay between the real GPS module and the Teensy isn't practical -- instead,
the GPS module's PPS output stays wired to the Teensy as normal, its NMEA TX line is disconnected,
and `mitm/`'s scripts drive the Teensy's `GPS_SERIAL` input directly over a single USB-UART with
entirely pre-generated, fully fabricated NMEA sentences. Real GPS PPS edges land almost exactly on
the UTC second boundary, so timing sends off this machine's own wall clock is plenty accurate for
the 950ms lag tolerance without needing to observe the real PPS signal. Chosen over a second-MCU
relay (Teensy/STM32/ESP32 with two UARTs): the actual hard part is the scenario logic (date
arithmetic, multi-fix sequencing, rebasing a feed), much faster to write/debug in Python than as
embedded C with a reflash cycle between iterations.

- [x] **`mitm/nmea.py`**: ZDA/RMC/GGA sentence builders with a checksum function (XOR of every
      byte between `$` and `*`) cross-checked independently against `GPS.cpp`'s own worked
      example (`$GPZDA,174304.36,24,11,2015,00,00*66`) rather than just trusting its own logic.
- [x] **`mitm/generate.py`**: builds finite fixture files for 6a (spurious duplicate `D`), 6b
      (backwards `D`), and 6c (leap-second stall `L`) as groups of sentences separated by `---`
      markers, with per-scenario setup reminders embedded as comments (6a/6b need an
      already-synced device; 6c needs a power-cycle first). `--sentence-type zda|rmc` matches
      whichever the module in use actually emits, since `GPS.cpp`'s own detection is runtime-based.
- [x] **`mitm/player.py`**: replays a fixture file at one group per real second, at a configurable
      millisecond offset from the top of the second -- tested standalone (no hardware), wakes
      consistently at the target offset with no drift across iterations.
- [x] **`mitm/rebase_relay.py`**: section 7a/7c (Y2036 wraparound) can't be a finite fixture --
      it needs to keep relaying through the wrap and potentially the whole ~4096s section-7c
      client-expiry soak afterward -- so this computes and sends a live rebased sentence every
      second indefinitely, seeded from a fixed offset chosen at startup.
      Given a `wrap`/`leap` subcommand split 2026-07-23: the 6c retest (see the "MITM bench
      session findings" TODO.md entry) showed `generate.py 6c`'s finite fixture gives only ~2s of
      margin around the leap instant, not enough to comfortably observe recovery. `leap` mode
      generalizes it -- open-ended like `wrap`, with a configurable lead-in, and three ways a
      real receiver might report the same leap second (`--leap-mode dup59/add60/dup00`): repeat
      the last regular second (what 6c already covers), send a literal `HH:MM:60` sentence
      (`DateTime`'s own leap arithmetic, untested against real hardware before this), or repeat
      the second *after* the leap instead of before (an ordinary duplicate, not leap-recognized,
      since `leapSecondStallSecond()` only matches a duplicate of the second *before* a compiled
      table entry). `nmea.py` gained `zda_leap_second()`/`rmc_leap_second()`/`leap_second_group()`
      for the literal-`:60` case, built from strings since Python's `datetime` can't represent
      second=60 at all. The sequencing logic (`leap_sequence()`) is a generator, tested standalone
      for all three modes without needing hardware.
- [x] Uses a `venv` under `mitm/` (gitignored, along with generated `fixtures/` and
      `__pycache__/`) rather than installing `pyserial` globally.
- [x] Ran all four scenarios (6a/6b/6c/7) against real hardware -- see "Y2036 wraparound in
      ClockDiscipline's monotonicity guard" and "ClockPID buffer reset across holdover recovery and
      bootstrap-phase leap seconds" below for what turned up (a real Y2036 `ClockDiscipline`
      wraparound bug, 6c's fixture being fundamentally blocked by the `compileSecondsTime` guard on
      current-era firmware, and new evidence on the already-tracked `ClockPID` buffer issue).

## ClockPID buffer reset across holdover recovery and bootstrap-phase leap seconds

- [x] **`ClockPID_c::reset_clock()` was dead code, letting one bad sample poison the whole
      16-entry regression window.** Raised by the 2026-07-22 holdover bench session
      (`holdover.txt`): after a long enough GPS/PPS outage, `calculate_d()`'s `remoteDuration *
      COUNTSPERSECOND` (`ClockPID.cpp:97`, `COUNTSPERSECOND` = 25,000,000 on real hardware) mixed a
      stale pre-holdover sample with fresh post-holdover ones spanning a `remoteDuration` large
      enough to overflow the 32-bit multiply, corrupting one `rawOffsets[]` entry; `chisq()` then
      overflowed `float` (printed literally as `ovf` by Arduino's `Print` class) for the ~4 samples
      it took to age the bad entry out of the window. Originally read as diagnostics-only (`ppb`/
      `pidD` stayed sane through the window) -- revised after the 2026-07-23 MITM bench session
      (`data/mitm`) saw the same `ovf` signature paired with `ppb` pinned to `NTPClock.cpp`'s
      `±500000` safety clamp and a ~27s `offsetHuman`, confirming a genuinely corrupted steered
      output, not just a display glitch.
      Section 6c (leap-second stall) as designed in `TESTPLAN.md`/`mitm/generate.py` couldn't run
      against normally-compiled (2026+) firmware at all -- all four fixture samples came back `B`
      (bad clock), correctly rejected by `gps_serial_poll()`'s `secondstime() < compileSecondsTime`
      guard before ever reaching `ClockDiscipline`, since the fixture's 2016/2017 dates are always
      older than the firmware's own build time. Retested with a temporary build (`compileTime`
      seeded from a hardcoded 2016-01-01 instead of `__DATE__`) to get past the guard, which is what
      surfaced the actual mechanism: a real leap second produces one sample with a genuinely
      anomalous ~1s offset (nothing steps `localClock`'s internal time across the inserted second --
      it only ever gets frequency corrections via `setPpb()`), and whether that sample corrupts
      anything depends entirely on whether `ClockPID`'s median-of-3 outlier rejection is active yet.
      During bootstrap (`pid_->full()` false), `ClockDiscipline::process()` always takes the
      immediate-resolve branch, so the anomalous sample goes straight into the regression
      unfiltered -- a 60s lead-in retest showed the corruption signature (`ovf`, clamped `ppb`) for
      the two samples after the `L` correction. A follow-up 180s-lead-in retest (long enough to
      clear bootstrap before the leap instant) came back completely clean, ruling out "`localClock`
      is permanently behind after every leap second" as the mechanism, since that would corrupt
      every subsequent sample forever, not just transiently -- in steady state, the anomalous
      sample loses the median-of-3 comparison against two normal neighbors and never reaches
      `ClockPID` at all.
      Fixed `c5fdfab`: wired `reset_clock()` in at both known trigger points rather than leaving it
      dead. `teensy-ntp.ino`'s `updateTime()` calls `ClockPID.reset_clock()` unconditionally
      whenever `holdover.inHoldover()` is true, right before `discipline.process()` -- discarding
      the stale pre-outage history instead of mixing it with fresh post-recovery samples.
      `ClockDiscipline::process()`'s leap-stall-correction branch calls `pid_->reset_clock()` only
      `if (!pid_->full())` -- narrow on purpose, so an ordinary leap second during steady-state
      operation (median-of-3 already filters it) doesn't needlessly throw away a perfectly good
      regression history; only the bootstrap case, which has no such protection, gets reset.
      `test/test-ClockDiscipline.cpp` gained `test_leap_stall_resets_pid_buffer_during_bootstrap`
      (5 unrelated real samples accumulated first, so "count ends up equal to before" can't be a
      coincidence of starting from a single sample -- confirms a genuine reset, not an append) and
      `test_leap_stall_does_not_reset_pid_buffer_when_full` (same correction with the buffer
      already full via the existing `fillPidFull()` helper, confirms it's left untouched in steady
      state); the pre-existing `test_duplicate_gpstime_at_leap_stall_is_corrected`'s final assertion
      changed from `pid.samples() > samplesBefore` to `== samplesBefore` to match the
      reset-not-append behavior. All 113 host-side tests passed at the time; firmware compiled
      clean via `./compile.sh`. Not yet re-verified against real hardware (the holdover half needs
      a real outage, the leap half needs the `mitm/` rig again) -- also confirmed working correctly
      in both 6c retests regardless of this fix: the NTP Leap Indicator field (`+1s (64)` before the
      transition, cleanly clearing to `(0)` after) and continuously correct served timestamps
      throughout.

## Y2036 wraparound in ClockDiscipline's monotonicity guard

- [x] **Critical, would have affected every deployed unit in 2036.** `ClockDiscipline::process()`'s
      monotonicity guard (`ClockDiscipline.cpp:48`) compared raw `TaiNtpTime.v` values with a bare
      `if (gpstime.v < lastGpstime_.v)`. `TaiNtpTime` (`NtpTimestamp.h:35`) is monotonic across a
      *leap second* but is still a plain wrapping `uint32_t` numerically -- it wraps at the same
      2036-02-07 06:28:16 UTC instant as wire format. Found during the 2026-07-23 MITM bench session
      (`mitm/rebase_relay.py wrap`): once the rebased clock crossed the wrap, every subsequent real
      sample (`0 < 4294967293`, `1 < ...`, etc.) compared as "backwards" and was rejected (`D 0`
      through `D 10`, and unboundedly after -- `lastGpstime_` never updates on a rejected sample, so
      it couldn't recover until the *next* 2^32-second wrap). Same class of bug `817ce85` already
      fixed for `gps_serial_poll()`'s sanity check (see "NTP timestamp era rollover (Y2036)" above),
      but `ClockDiscipline`'s own comparison never got the same treatment. Compounded by two things
      that made the failure silent rather than visibly broken (both since fixed -- see "WebContent
      gpstime freeze during holdover" and "noteSampleReceived() ordering" below):
      `holdover.noteSampleReceived()` fired unconditionally regardless of accept/reject, so
      holdover's staleness timer never tripped; and `WebContent::setPPSData()` (called before the
      accept/reject decision) echoed the raw incoming `gpstime`, so the web UI's "NTP time" field
      kept advancing normally even while the actual disciplined `localClock` was silently frozen at
      its last pre-wrap `ppb`, free-running forever with no correction and no alarm visible
      anywhere.
      Fixed `b2a4027`: replaced the bare `<` with `elapsedWithin(gpstime.v, lastGpstime_.v,
      0x7fffffff, &forwardGap)` (`Elapsed.h`), the same wraparound-safe forward-gap pattern
      `NTPClients.cpp` already uses for exactly this reason. `0x7fffffff` (~68 years) as the window
      is deliberately enormous -- large enough that no legitimate gap between consecutive accepted
      samples (even a very long holdover outage) is ever mistaken for backwards, while still
      correctly rejecting a genuine backwards jump (which wraps to a huge gap under this
      arithmetic). `forwardGap == 0` replaces the old `gpstime.v == lastGpstime_.v` duplicate/
      leap-stall check, unchanged otherwise.
      One pre-existing test (`test_leap_stall_resets_pid_buffer_during_bootstrap`, added alongside
      the `ClockPID` fix above) had to be adjusted: its lead-in samples jumped from an arbitrary
      unrelated epoch straight to the 2017 leap-second boundary, a gap that (correctly) now trips
      the same guard as implausible -- changed to count up realistically toward the stall second
      instead. Added `test_forward_sample_accepted_across_y2036_wraparound` (a real sample right
      after the wrap, at `v=0` and `v=1`, is accepted, not rejected) and
      `test_implausibly_large_forward_gap_still_rejected` (an exactly-half-range forward gap is
      still rejected, confirming the fix doesn't turn the guard into a no-op). `test/Makefile`'s
      `test-ClockDiscipline` link rule gained `Elapsed.o`. All 112 host-side tests (11 binaries, 2
      new in this file) pass; firmware compiles clean via `./compile.sh`.

## WebContent gpstime freeze during holdover

- [x] **`WebContent`'s displayed "NTP time" field froze during a holdover episode.** The web UI's
      `gpstime` (`index_html.h:18`'s "NTP time" field) was only ever updated from real GPS-derived
      samples (`setPPSData()`/`setLocalClock()`), with a `haveGpsTime` flag gating a fallback to
      `localClock`'s live time -- added earlier so the field wouldn't sit stuck at the NTP epoch
      before the first-ever GPS fix across a power cycle. That flag latched permanently true on the
      first real fix, so during a holdover episode (GPS/PPS gone silent after already having synced
      once), `gpstime` froze at the last real sample instead of falling back live, freezing the web
      UI graphs the same way they used to freeze at the epoch pre-fix. Identified in the 2026-07-22
      holdover bench session (`holdover.txt`).
      First fix: changed the fallback condition in `jsonState()` from `!haveGpsTime` to
      `!haveGpsTime || inHoldover`, so the live-clock fallback also covers holdover. Compiled and
      tested clean (112 host-side tests), but immediately raised the obvious follow-up question:
      why maintain two conditions and a separate `gpstime`/`haveGpsTime` state at all, when the
      field is labeled "NTP time" (not "GPS time") and is supposed to represent what the server is
      currently serving?
      **Superseded same day by a simpler redesign**: `WebContent` no longer tracks a GPS-derived
      `gpstime` or `haveGpsTime` at all. `jsonState()` unconditionally reads `localClock.getTime()`
      and converts via `taiToWireNtp()` -- `setPPSData()`/`setLocalClock()` dropped their now-unused
      `TaiNtpTime new_gpstime` parameter entirely (call sites in `teensy-ntp.ino`'s `updateTime()`
      updated to match). This removes the whole freeze bug class by construction rather than
      patching each freeze scenario as it's found (there's no flag to latch, no window with nothing
      to show), and as a side effect displays a smoothly, continuously advancing time instead of
      jumping in `ClockDiscipline`'s ~48s steady-state resolve cadence. Tradeoff noted but accepted:
      `gpstime` is no longer snapshotted at the same instant as `offsetHuman`/`pidD`/`dChiSq` (which
      still only update once per resolve), so the displayed clock keeps ticking while those
      diagnostic fields sit at their last resolved value -- not a correctness issue, since staleness
      of the diagnostics is already visible separately via the holdover/dispersion fields.
      This also resolves, as a side effect, half of the MITM-session compounding-visibility-gap item
      (see "noteSampleReceived() ordering" below for the other half): `WebContent::setPPSData()` no
      longer echoes an unaccepted raw `gpstime` at all, since there's no `gpstime` field left to
      echo.
      Firmware compiles clean via `./compile.sh`; all 112 host-side tests still pass (`WebContent`
      itself has no host-side tests -- it depends directly on lwIP/Teensy hardware APIs, per
      `CLAUDE.md`).
      Also *appeared* to resolve this same GPS satellite strong/weak signal counts sometimes
      jumping abruptly (e.g. 5→39) -- observed during the 2026-07-22 holdover bench session, never
      root-caused at the time, not reproduced immediately after this fix (reported 2026-07-23), so
      logged here as a working theory (display artifact of the old frozen `gpstime`'s chart x-axis).
      **Wrong -- see "Satellite signal counts accumulating without bound during holdover" below**,
      found on a longer holdover run: the real cause was unrelated to this fix and just hadn't been
      exercised for long enough yet to reproduce.

## Web UI: show the raw GPS-reported date/time

- [x] **Show the GPS module's own raw NMEA date/time on the status page, distinct from the served
      "NTP time" field.** Raised while fixing the `gpstime` freeze above: once `WebContent` was
      redesigned to always display `localClock`'s live time, there was no longer *any* field on the
      page reflecting what the GPS module itself is actually reporting. After a cold start, a valid
      GPS-reported date is the second sign of life the module gives (after satellite counts start
      climbing, before PPS/lock are fully established) -- useful "is it making progress yet"
      information an operator otherwise has no way to see from the web UI.
      `WebContent` gained a new `setGpsTime(TaiNtpTime)` plus a `gpsReportedTime`/
      `haveGpsReportedTime` pair (converted via `taiToWireNtp()`, same pattern the old `gpstime`
      field used) -- deliberately its own separate flag-gated state, unlike the `localClock`
      redesign above, since "no GPS sentence has committed yet" is exactly the condition this field
      exists to show, not a freeze bug to design around. `teensy-ntp.ino`'s `gps_serial_poll()` calls
      `webcontent.setGpsTime(gpstime)` right after `gps.decode()` succeeds, *before* the
      `compileSecondsTime` sanity check and before `updateTime()`/`ClockDiscipline` -- so it reflects
      whatever the GPS module just said even if that gets rejected downstream (an implausible
      pre-almanac date at cold start is itself useful evidence the module is alive and parsing).
      `jsonState()` adds both values to the JSON blob; `index_html.h` gained a new "GPS reported
      time" `<p>` mirroring the existing "NTP time" raw+human layout; `index_js.h` renders an ISO
      date when `haveGpsReportedTime` is set, `(none yet)` beforehand.
      Firmware compiles clean via `./compile.sh`; all 112 host-side tests still pass (`WebContent`/
      `WebServer` have no host-side tests, per `CLAUDE.md` -- not verified visually in a browser,
      only that the JSON/JS logic follows the same pattern the existing `gpstime`/`gpstimeHuman`
      fields already use).

## Web UI: reorganize the flat stat list into a table

- [x] **The flat `<p>`-per-line list of stats at the bottom of the status page was hard to scan.**
      Reorganized `index_html.h` into five labeled tables instead -- Clock (NTP time, GPS reported
      time, NTP/GPS offset, 1588 counter), Clock discipline/PID (drift estimate, ChiSq fit, PID
      output), Holdover (in-holdover flag, dispersion estimate, elapsed), PPS/GPS timing
      diagnostics (the four raw `millis()`/lag fields), and GPS reception (lock status, signal
      counts, DOP). Also split the previously-combined "Strong/Weak/No signal" line into one row
      per band, so each is independently labeled rather than packed into a single comma-separated
      cell. A small `<style>` block (`border-collapse`, muted non-bold `<th>` labels, tight padding)
      keeps the tables compact rather than defaulting to the browser's bordered/spaced table look.
      All existing `span` element ids are unchanged, so `index_js.h` (which populates them by id via
      `$.each(json, ...)`) needed no changes at all. Firmware compiles clean via `./compile.sh`; not
      verified visually in a browser (`WebContent`/`WebServer` have no host-side tests, per
      `CLAUDE.md`).

## noteSampleReceived() ordering: rejected samples no longer count as holdover-fresh

- [x] **`holdover.noteSampleReceived()` fired unconditionally, before checking whether
      `ClockDiscipline` actually accepted the sample.** Flagged alongside the Y2036 wraparound fix
      (see above) -- back then a rejected sample was always a *transient* glitch (a single duplicate
      or a brief stall), so resetting holdover's staleness timer regardless of accept/reject looked
      harmless. The wraparound bug showed the real risk: a *sustained* run of rejections (there, once
      every real sample after the wrap; in general, any future bug, a wedged `lastGpstime_`, or a
      misbehaving GPS module) would never trip holdover, since *something* kept arriving every
      second even though none of it was trusted -- no `inHoldover`, no growing dispersion, no
      stratum-16 fallback, even though the served clock wasn't being disciplined at all. This also
      directly contradicted `ClockHoldover.h`'s own documented contract for the method ("call
      whenever `ClockDiscipline::process()` *accepts* a sample"), which the call site never actually
      honored.
      Fixed as part of extracting `updateTime()`'s orchestration logic into `UpdateTimeCore.cpp`
      (see "extract testable pieces out of the untested teensy-ntp.ino" -- this fix landed as a
      follow-up in the same new file): `holdover.noteSampleReceived(nowMillis)` is now called only
      `if(!outcome.discipline.rejected)`, after `discipline.process()` returns, instead of
      unconditionally right after it. `test-UpdateTimeCore.cpp`'s
      `test_rejected_sample_still_resets_holdover_staleness_timer` (which characterized the old
      behavior) was rewritten to `test_rejected_sample_does_not_reset_holdover_staleness_timer` --
      a rejected sample arriving more than `HOLDOVER_STALE_MS` after the last *accepted* one now
      correctly leaves holdover active. Added
      `test_accepted_sample_still_resets_holdover_staleness_timer` alongside it, confirming the fix
      doesn't overcorrect into never resetting the timer for genuinely trusted samples. All 15
      host-side test binaries (130 tests) pass; firmware compiles clean via `./compile.sh`.

## Web UI: replace "Holdover elapsed" with a UTC "Holdover started at" timestamp

- [x] **"Holdover elapsed" (a live-ticking ms-based duration) didn't line up with any other
      displayed time, and a client polling `state.json` only once/sec would always see it up to a
      second stale anyway.** Raised in the 2026-07-22 holdover bench session (`holdover.txt`).
      First attempt added a second field (`msSinceLastGoodSample`) alongside the existing
      `holdoverElapsedMs`, to separate "how long the holdover *episode* has run" (only starts
      counting once `HOLDOVER_STALE_MS` has already passed) from "how long since GPS/PPS was
      actually last heard from." Rejected on review: two duration fields that differ only by a
      constant 4s offset would be confusing to a reader, not clarifying -- and both are still
      live-ticking durations, so the "up to 1s stale from a once/sec poll" problem remained either
      way.
      **Redesigned as a single fixed UTC timestamp instead of a duration.** `ClockHoldover`'s
      `noteSampleReceived()` now also takes the accepted sample's own `TaiNtpTime`, stored as
      `lastGoodGpsTime_`. `HoldoverStatus` gained `holdoverStartTime` (`TaiNtpTime`), computed once
      the episode goes stale as `lastGoodGpsTime_ + HOLDOVER_STALE_MS/1000` (exact integer seconds,
      not an approximation) -- only meaningful when `inHoldover` is true, and constant for the
      whole episode rather than advancing every poll, so a client polling once/sec (or less often)
      always sees the same correct value, not a stale snapshot of a moving target. `elapsedMs`
      stays in `HoldoverStatus` for `growDispersion()`'s internal math and test verification, but is
      no longer surfaced to the web UI.
      `WebContent::setHoldover()` now takes this `TaiNtpTime` instead of the old
      `holdoverElapsedMs`/`uint32_t`, converting via `taiToWireNtp()` like the other displayed
      timestamps. `index_html.h`'s "Holdover elapsed" row became "Holdover started at" (raw + ISO
      date, matching the `gpstime`/`gpsReportedTime` raw+human pattern); `index_js.h` shows the
      timestamp only while `inHoldover` is true, `"n/a"` otherwise.
      `test-ClockHoldover.cpp` gained `test_holdover_start_time_is_last_good_sample_plus_stale_window`
      (confirms the exact value and that it stays constant across repeated polls within the same
      episode) and `test_fresh_sample_resets_holdover_state` was extended to confirm a new baseline
      sample updates `holdoverStartTime` to the new episode, not the previous one's. All 15
      host-side test binaries (131 tests) pass; firmware compiles clean via `./compile.sh`.

      **Separately, also addressed:** "NTP time" and "GPS reported time" occasionally appearing out
      of sync on the status page. Root cause isn't a race: "NTP time" is `localClock`'s
      continuously-running live time, while "GPS reported time" only updates once per second
      (whenever a new NMEA sentence arrives) and is whole-second resolution -- up to ~1s of skew
      between a continuously-updated value and a once-a-second discrete one is expected, not a
      fault. Considered and rejected: hiding "GPS reported time" unless in holdover -- this field's
      whole purpose is cold-start visibility (seeing the GPS module report *any* date before
      PPS/lock are established, see "Web UI: show the raw GPS-reported date/time" above), which
      happens *before* holdover's state machine has even started (`everSeen_` is false), so hiding
      it "unless in holdover" would suppress it during exactly the window it exists for.
      `index_js.h` instead collapses the display to `"in sync"` whenever the two are within 1
      second of each other, and only reveals the raw timestamp (for debugging) once the gap is
      large enough to actually mean something. `index_html.h`'s "GPS reported time" row collapsed
      from separate raw+human spans to a single status span (`gpsReportedTimeStatus`) to match.

- [x] **"GPS reported time" (and other web UI fields) freezing during a real holdover episode,
      found via bench testing with PPS physically disconnected (`data/run3/notes`).** Root cause:
      `GPSDateTime::commit()` (GPS.cpp) only fires once per real PPS pulse, gated by
      `committedThisPulse_`, which is only reset back to `false` inside `decodeType()` when
      `pps.getCaptures()` has advanced (see "Snapshot the PPS/local-time reference..." comment
      there). Once PPS stops advancing entirely, that reset never happens again, so `commit()`
      never runs again either -- and `decode()`'s return value (`isUpdated_`) is only ever set
      inside `commit()`'s gated branch. `gps_serial_poll()` only called `webcontent.setGpsTime()`
      and `updateTime()` from inside `if(gps.decode())`, so once PPS was lost, *both* stopped being
      called at all: not just "GPS reported time" but `offsetHuman`/`pidD`/`dChiSq` too (all only
      written from `updateTime()`'s dispatch), and no more "LAG"/"B" serial messages either -- even
      though the GPS module kept sending valid, checksum-passing NMEA sentences the whole time.
      This directly defeated the point of "GPS reported time" (see above): it's meant to show
      progress "regardless of whether ClockDiscipline trusts it," and PPS-loss/holdover is exactly
      the scenario that needed it most.
      Fixed by decoupling "the web UI's raw GPS-reported time" from `commit()`'s once-per-pulse
      gate, rather than loosening that gate itself (which exists to stop `updateTime()` processing
      the same PPS pulse twice when a module emits both ZDA and RMC — left untouched, so the
      clock-discipline path's behavior doesn't change at all). `GPSDateTime` gained `reportUpdated_`
      (set on every checksum-valid ZDA/RMC sentence, independent of `committedThisPulse_`, consumed
      via a new `reportedUpdate()` getter) and `reportedNow()` (a `DateTime` built from the
      not-yet-committed `newTime_`/`newYear_`/`newMonth_`/`newDay_` fields, which
      `decodeTimeCode()` already updates on every sentence regardless of the commit gate).
      `teensy-ntp.ino`'s `gps_serial_poll()` now calls `webcontent.setGpsTime(gps.reportedNow()...)`
      whenever `gps.reportedUpdate()` is true, separately from the `if(committed)` block that still
      gates `updateTime()`/the `B` check on `gps.decode()`'s original, unchanged return value.
      `test-GPS.cpp` gained `test_reported_update_survives_pps_loss` (a second ZDA sentence with no
      intervening PPS pulse still updates `reportedNow()`/`reportedUpdate()` even though `decode()`
      correctly still returns `false` and `GPSnow()` stays frozen) and
      `test_reported_update_is_consumed_on_read`.

- [x] **Compiler warnings on `DisciplineResult discipline = {};` (UpdateTimeCore.h) and similar
      value-initialized structs, flagged from a real firmware build.** `WireNtpTime`/`TaiNtpTime`
      (NtpTimestamp.h) each had a single `explicit Type(uint32_t seconds = 0)` constructor doing
      double duty as both the converting and default constructor; GCC warns whenever a containing
      aggregate (`DisciplineResult`, `HoldoverStatus`, ...) is value-initialized with `{}`/`= {}`,
      since that recursively list-initializes the `TaiNtpTime`/`WireNtpTime` member through the
      same constructor an implicit conversion would use. Fixed by splitting each into two
      constructors -- a plain `Type()` and a separate `explicit Type(uint32_t seconds)` -- so `{}`
      unambiguously picks the non-explicit one. No behavior change; confirmed via `test/`'s g++
      build (same warning flags) that the exact warning is gone and all 15 test binaries still
      pass.

- [x] **`-Wformat-overflow` warning on `DateTime::toString()`'s `sprintf(chartime, "%02d:%02d:%02d",
      hour(), minute(), second())`, flagged from a real firmware build.** `hour()`/`minute()`/
      `second()` return `uint16_t` with no enforced 0-59 bound, so GCC's static analysis correctly
      flags that a large value could overflow the 9-byte `chartime` buffer -- a real latent
      stack-buffer overflow, not just a false positive, if those fields were ever out of range.
      `toString()`/`chartime` had no callers anywhere in the repo (grepped, including tests), so
      rather than patch the `sprintf` (`snprintf`, clamp the fields, ...), deleted `toString()` and
      the now-unused `chartime` buffer outright. `DateTime::print(Stream*)` (also unused, but out
      of scope for this warning) was left as-is.

## Satellite signal counts accumulating without bound during holdover

- [x] **`weakSignals`/`strongSignals`/`noSignals` growing to absurd values (1436 weak signals)
      after a ~5 minute holdover, found via bench testing (`data/run4/notes`).** Same root cause
      class as "GPS reported time" freezing during holdover, above -- but a worse symptom, because
      `GPSDateTime::decodeGSV()`'s accumulation is additive, not overwriting. `strongSignalNext`/
      `weakSignalNext`/`noSignalNext` are incremented once per satellite entry across a GSV burst
      (there's no bound on how many bursts contribute before they're read), and used to only get
      published into `strongSignal`/`weakSignal`/`noSignal` (and reset to 0) inside `commit()`'s
      `sawGSV`-gated block -- which, like everything else in `commit()`, only fires once per real
      PPS pulse. Once PPS was lost, `commit()` stopped firing entirely, but the GPS module kept
      sending GSV sentences once/sec regardless, so the `Next` counters kept accumulating every
      second of the whole holdover episode with nothing to ever reset them; when PPS finally
      returned and `commit()` ran again, the multi-minute accumulated total published in one shot.
      This is exactly why the "5→39" jump logged above (before the once-per-second
      `gpstime`/reported-time freeze was fixed) looked plausible as a display artifact at the time
      -- a short stall only accumulates a little -- but a long enough holdover exposes the real,
      unbounded growth underneath.
      Fixed the same way as `reportedUpdate()`/`reportedNow()`: moved the publish-and-reset step out
      from behind `commit()`'s `committedThisPulse_` gate, into `decode()`'s checksum-valid branch
      for every ZDA/RMC sentence (regardless of whether `commit()` itself ends up running for it).
      Considered and rejected: keying the reset/publish off GSV's own "total messages"/"message
      number" fields instead -- multi-constellation receivers (GPS+GLONASS, etc.) emit separate
      GSV groups per talker within the same one-second cycle (see `test_satellites`'s GPGSV+GLGSV
      mock data), and the existing design deliberately merges all of them into one listing; keying
      off a single talker's own message-complete field would flush (and reset-clobber) the combined
      count mid-cycle. Publishing on ZDA/RMC instead preserves that merge behavior exactly, since
      it's the same trigger point `commit()` already used -- just no longer gated on PPS. `commit()`
      itself is now just the four date/time field copies.
      `test-GPS.cpp` gained `test_satellite_counts_reset_each_cycle_during_pps_loss`: feeds the same
      GSV+ZDA cycle twice with only one `firePulse()` call (simulating PPS staying lost across both),
      and asserts the second cycle's counts match the first cycle's alone, not the two summed.

## Y2036 leap-second offset lookup breaks permanently after the wire-timestamp wrap

- [x] **`LeapSeconds.cpp`'s `leapSecondOffsetAt(WireNtpTime)`/`leapSecondOffsetAtTai(TaiNtpTime)`
      did a plain `<=`/`taiBoundary <= taiTime.v` linear scan against the table with no
      wraparound handling** -- unlike `ClockDiscipline`'s monotonicity guard,
      `NTPClients::expireClients()`, and the `gpstime`-vs-`compileTime` sanity check, which all
      got the `elapsedWithin()` treatment for this same 2036-02-07 06:28:16 UTC wire-domain
      wraparound. `DateTime::ntptime()`'s internal `leapOffsetFor()` recomputes the same
      wire-domain value and feeds it straight into `leapSecondOffsetAt()`; once that value wraps
      to a small number, every table entry (all in the billions) looked larger than it, the scan
      found no match, and the function silently returned 0 instead of the real cumulative offset
      (37, as of the current table) -- **permanently**, since the wrapped domain won't reach the
      table's range again for ~136 years.
      Found via bench testing (`data/run7`, `rebase_relay.py wrap`): `ClockDiscipline` saw a
      genuine (but bogus, offset-caused) ~-37s jump right after the wrap, pinned `ClockPID` at its
      -500ppm clamp, and spent several minutes clawing `localClock` down by 37 real seconds to
      match the now-permanently-wrong GPS samples -- not correcting anything, just losing time.
      Also explains a subtlety in that same bench run: `TaiNtpTime.v` (`= wireT + offset`)
      overflows ~37s *before* `wireT` itself does (adding the offset pushes the sum past 2^32
      that much earlier), so there's a window right at the crossing where `TaiNtpTime.v` has
      already wrapped but the (not-yet-buggy) offset lookup on the not-yet-wrapped `wireT` is
      still correct -- clean samples cycle through 0..36 here. Only once `wireT` itself wraps does
      the bug trigger, and `TaiNtpTime.v` (now computed with the wrong offset) genuinely jumps
      back ~37s -- correctly rejected by `ClockDiscipline`'s monotonicity guard as an implausible
      backward jump (the `D <n>` storm observed in that run), not a regression in that guard's own
      wraparound fix (`b2a4027`).
      **Test-driven fix**: added `test_offset_wraps_around_y2036_correctly`/
      `test_offset_at_tai_wraps_around_y2036_correctly` to `test-LeapSeconds.cpp` first (using
      `34`, the actual wrapped value observed on the bench), confirmed both failed
      (`Expected 37 Was 0`) against the unfixed code, then fixed `leapSecondOffsetAt()`/
      `leapSecondOffsetAtTai()` to scan via `elapsedWithin(ntpTime.v/taiTime.v, entry, 0x7fffffff,
      &gap)` instead of plain `<=` -- the same window already used for this exact wraparound
      elsewhere. All existing tests in that file (including the "holds forever after the last
      entry" and TAI-domain boundary/leap-instant cases) still pass unchanged.
      One pre-existing test in `test-NTPClients.cpp`
      (`test_expireClients_survives_wire_domain_wrap`) relied on the *old* bug as a simplification
      -- it deliberately used tiny `TaiNtpTime`/`WireNtpTime` values assuming they'd read as
      "before 1972" (offset 0) to sidestep leap-second conversion entirely. With the fix, a small
      `TaiNtpTime` now correctly reads as "shortly after the 2036 wrap" (offset 37) instead, so
      that test's chosen numbers no longer meant what its comment said. Updated it to use
      `TaiNtpTime(40)` (still small, but now consistently "3 wire-seconds after the wrap" once
      converted) with `wireRx` adjusted to match, preserving its actual intent (verify
      `NTPClients::expireClients()`'s own wraparound-safe comparison, independent of leap-second
      conversion).
      `leapSecondPendingToday()` has the same non-wraparound-safe pattern but much lower practical
      stakes (only matters if a leap second is scheduled exactly at/near a future wraparound, not
      "forever after") -- left open, see TODO.md. `leapSecondStallSecond()` uses pure equality
      comparison and was already wraparound-safe; no change needed.
      `test/Makefile` gained `Elapsed.o` as a link dependency for `test-LeapSeconds`,
      `test-DateTime`, `test-GPS`, `test-GpsBaud`, and `test-NTPResponseFields`, all of which
      pull in `LeapSeconds.o` and now transitively need `elapsedWithin()`. All 15 host-side test
      binaries pass.
