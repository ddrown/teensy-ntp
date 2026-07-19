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
      Type safety added 2026-07-15 for the *other* mixed-domain risk in this area -- not
      wraparound, but the wire-format-vs-TAI-like NTP-seconds confusion introduced when
      `DateTime::ntptime()` became leap-second-aware (see `DONE.md`, "Leap second handling").
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

## NTP timestamp era rollover (Y2036) (open issue)

- [ ] **Design: compare firmware-internal timestamps in seconds-since-2000, not wire-format
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

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.
