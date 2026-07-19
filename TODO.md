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

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.
