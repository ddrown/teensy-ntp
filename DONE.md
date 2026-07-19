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
