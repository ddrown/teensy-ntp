# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## GPS lock lost / PPS stopped (open issue)

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
      gap. Wired into `ClockHoldover`'s staleness and per-poll-tick duration accumulation and, as
      of 2026-07-14, the existing `ppsToGPS` lag check site too (see `DONE.md`) -- no known
      remaining raw *millis()-domain* duration subtraction outside `Elapsed.h` itself and
      `webcontent`'s cosmetic display value. Not yet applied to the NTP-seconds domain:
      `NTPClients::expireClients()` (NTPClients.cpp:55) still computes `expire_time = sec - 4096`
      via raw subtraction on NTP seconds, not milliseconds -- but this is a much lower-priority
      loose end than it first looks: NTP's 32-bit seconds field wraps roughly every
      2^32/(365.2425*86400) ≈ 136.19 years (the well-known "NTP rolls over in 2036" period), not
      days or seconds like `millis()`/the 1588 counter, so actually hitting that wraparound would
      require this specific device to run continuously for many decades -- not a realistic risk
      for this deployment. Left untouched by this round; noted for completeness, not urgency.

## Leap second handling (open issue)

- [ ] **Design: compiled-in leap-second table, step-only (no smear), no web override.**
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

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.
