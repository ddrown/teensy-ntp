# TODO

Notes from a code review, roughly ordered by priority.

## GPS lock lost / PPS stopped (open issue)

- [ ] **Design: degrade gracefully via D-only holdover + growing estimated dispersion, instead
      of (or in addition to) a hard watchdog timeout.** Preferred shape, discussed 2026-07-13:
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
- [ ] **The existing lag check is wraparound-unsafe for long outages.**
      `ppsToGPS = gps.capturedAt() - gps.ppsMillis()` (teensy-ntp.ino:161) relies on unsigned
      subtraction of two `millis()` values, which only gives a correct "looks small" result if
      the true gap is under ~24.8 days (half of the 32-bit `millis()` wrap period, ~49.7 days).
      If PPS is dead for longer than that, the wrapped subtraction can come back around into
      "looks fresh" territory and silently pass the `> 950` check again. Any new watchdog logic
      needs its own explicit bounded-window wraparound-safe comparison — it can't reuse this
      same subtraction trick unmodified if it's meant to catch outages that can run indefinitely.
- [ ] **Mixed timestamp domains make this easy to get wrong twice.** The codebase compares/
      subtracts across at least three different clock domains without a shared abstraction:
      `millis()` (1 kHz, wraps ~49.7 days — used by `InputCapture`/`GPSDateTime` for
      `dateMillis`/`ppsMillis_`), the hardware 1588/`COUNTERFUNC()` counter (wraps much sooner —
      see the existing comment "`(2+1)*16=48s, 80MHz wraps at 53s`", teensy-ntp.ino:189), and
      GPS-native NTP seconds. Before adding staleness detection, worth introducing one
      wraparound-safe "elapsed(now, then)" helper with an explicit max-valid-window, used
      everywhere a duration is computed, rather than continuing to sprinkle ad hoc unsigned
      subtraction around — otherwise the watchdog fix risks introducing its own wraparound bug.

## Bugs / fragile areas

- [ ] Rewrite `GPSDateTime::decodeType()` (GPS.cpp:96) — the `#ifdef GPS_USES_RMC` branch
      interleaves an `if/else if` chain across preprocessor boundaries (RMC/GGA branch vs. ZDA
      branch are stitched together by the preprocessor). This shape produced issue #6 ("code
      assumes RMC comes after GGA") and contributed to #5 ("checksums in the range 0x0..0xF
      fail"). Consider a table/struct-driven parser: an array of
      `{sentence_code, field_count, handler_fn}` selected at *runtime* based on which sentences
      are actually seen, rather than selected at compile time. Would also allow supporting
      ZDA-only, RMC+GGA, or both without recompiling.
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
