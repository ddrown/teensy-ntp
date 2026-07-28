# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Y2036 leap-second offset lookup breaks permanently after the wire-timestamp wrap

- [ ] `LeapSeconds.cpp`'s `leapSecondOffsetAt(WireNtpTime)` and `leapSecondOffsetAtTai(TaiNtpTime)`
      do a plain `<=` linear scan against the table with no wraparound handling -- unlike
      `ClockDiscipline`'s monotonicity guard, `NTPClients::expireClients()`, and the
      `gpstime`-vs-`compileTime` sanity check, which all got the `elapsedWithin()` treatment for
      this exact 2036-02-07 06:28:16 UTC wire-domain wraparound (see DONE.md). `DateTime::
      ntptime()`'s internal `leapOffsetFor()` (DateTime.cpp:43-46) recomputes the same wire-domain
      value and feeds it straight into `leapSecondOffsetAt()`; once that value wraps to a small
      number, every table entry (all in the billions) looks larger than it, the scan finds no
      match, and the function silently returns 0 instead of the real cumulative offset (37, as of
      the current table) -- **permanently**, since the wrapped domain won't reach the table's
      range again for ~136 years.
      Subtlety that explains the exact bench symptom below: `TaiNtpTime.v` (`= wireT + offset`)
      overflows **37 seconds before** `wireT` itself does, since adding the offset pushes the sum
      past 2^32 that much earlier. So there's a ~37s window right at the crossing where
      `TaiNtpTime.v` has already wrapped to a small number but `wireT` (used *inside*
      `leapOffsetFor()`) hasn't yet -- the offset is still looked up correctly there, and
      `TaiNtpTime.v` cleanly cycles through 0..36. Only once `wireT` *itself* finally wraps does
      the bug actually trigger: the offset silently drops to 0, and `TaiNtpTime.v` (now `wireT +
      0`) jumps back down to roughly 0 again -- a genuine ~37s backward discontinuity from
      `ClockDiscipline`'s point of view, correctly rejected by its monotonicity guard as an
      implausible backward jump (`D 0`, `D 1`, ... until the buggy, offset-less sequence climbs
      back up past the last-accepted value and gets waved through as if it were a normal +1s
      step) -- this is what produced the extended `D <n>` storm in `data/run7`, not a regression
      in `ClockDiscipline`'s own wraparound fix (`b2a4027`). Once that guard lets a
      post-wrap sample back in, `localClock` is disciplined against permanently-offset (missing
      37s) GPS samples from then on -- `ClockPID` pins at its -500ppm clamp for several minutes
      clawing 37 *real* seconds off `localClock` to match, i.e. losing time, not correcting
      anything.
      `leapSecondOffsetAtTai()` has the identical vulnerability against `taiTime.v`, so it also
      affects `DateTime::time(TaiNtpTime)` (decoding) and `taiToWireNtp()` -- used throughout
      `WebContent`/`NTPServer` for anything going out to the web UI or onto the wire.
      `leapSecondPendingToday()`/`leapSecondStallSecond()` have the same non-wraparound-safe
      pattern but much lower practical stakes (only matters if a leap second is scheduled exactly
      at/near the 2036 crossing itself, not "forever after").
      Found via bench testing (`data/run7`, `rebase_relay.py wrap`). Fix approach: the same
      `elapsedWithin()`-style wraparound-safe comparison already used elsewhere for this exact
      wraparound instant, applied to all four lookup functions in `LeapSeconds.cpp`.

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.
