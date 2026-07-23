# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.

## Holdover bench session findings (2026-07-22)

From a real PPS-disconnect holdover test (see `holdover.txt`), roughly in priority order.

- [ ] `ClockPID`'s sample buffer is never cleared across a holdover episode --
      `ClockPID_c::reset_clock()` exists (`ClockPID.h:46`) but is dead code, never called anywhere.
      After a long enough GPS/PPS outage, `calculate_d()`'s `remoteDuration * COUNTSPERSECOND`
      (`ClockPID.cpp:97`, `COUNTSPERSECOND` = 25,000,000 on real hardware) mixes a stale
      pre-holdover sample with fresh post-holdover ones spanning a `remoteDuration` large enough to
      overflow the 32-bit multiply, producing a garbage `rawOffsets[]` entry. `chisq()` then blows
      up to a value that overflows `float`, printed literally as `ovf` by Arduino's `Print` class,
      for the ~4 samples it takes to cycle the stale entry out of the 16-entry window. Originally
      observed `ppb`/`pidD` staying sane through this window and read it as diagnostics-only --
      **revise that**: the 2026-07-23 MITM bench session (see below) saw the same `ovf` signature
      accompanied by `ppb = 500000` and a ~27s `offsetHuman`, i.e. a genuinely corrupted steered
      output, not just a display glitch, when a single sample landed in the buffer inconsistently
      with its neighbors (there, most likely a stale fixture timestamp; in the original holdover
      case, the post-outage `remoteDuration` overflow). Either way, one bad point poisoning
      `ClockPID`'s whole 16-entry window until it ages out is the common thread -- wiring
      `reset_clock()` in wherever a sample is known to be a large/inconsistent jump (holdover
      recovery, and possibly `ClockDiscipline`'s own reject paths) is the likely fix.
- [ ] `WebContent`'s `gpstime` fallback to `localClock`'s live time (see DONE.md, "fall back to
      localClock's live time for gpstime before first GPS fix") only covers the window before the
      *first-ever* fix -- `haveGpsTime` latches true permanently once set. During a holdover
      episode (which happens after a real fix), `gpstime` freezes at the last real GPS sample
      instead of falling back live, so the web UI graphs freeze during holdover the same way they
      used to freeze at the NTP epoch before that fix.
- [ ] GPS satellite strong/weak signal counts sometimes jump abruptly (e.g. 5→39) -- observed
      during the 2026-07-22 holdover bench session, not yet investigated.
- [ ] Web UI: "Holdover elapsed" could show elapsed time since the last real PPS pulse instead (or
      in addition), so it lines up directly against "Reference time" for a reader comparing the two.
- [ ] Web UI: the flat `<p>` list of stats at the bottom of the status page is hard to scan --
      consider reorganizing into a table.

## MITM bench session findings (2026-07-23)

From running the `mitm/` tooling against real hardware for the first time (see `data/mitm`),
roughly in priority order.

- [ ] **Critical, will affect every deployed unit in 2036:** `ClockDiscipline::process()`'s
      monotonicity guard (`ClockDiscipline.cpp:48`, `if (gpstime.v < lastGpstime_.v)`) compares
      raw `TaiNtpTime.v` values directly. `TaiNtpTime` (`NtpTimestamp.h:35`) is monotonic across a
      *leap second* but is still a plain wrapping `uint32_t` numerically -- it wraps at the same
      2036-02-07 06:28:16 UTC instant as wire format. Confirmed on the bench: once the (rebased)
      clock crossed the wrap, every subsequent real sample (`0 < 4294967293`, `1 < ...`, etc.)
      compared as "backwards" and was rejected (`D 0` through `D 10`, and unboundedly after --
      `lastGpstime_` never updates on a rejected sample, so it can't recover until the *next*
      2^32-second wrap). This is the same class of bug `817ce85` already fixed for
      `gps_serial_poll()`'s sanity check (see DONE.md, "NTP timestamp era rollover (Y2036)"), but
      `ClockDiscipline`'s own comparison never got the same treatment. Fix should reuse the
      existing wraparound-safe pattern (`Elapsed.h`'s `elapsedWithin()`, already used for this
      exact purpose in `NTPClients.cpp`) instead of a bare `<`.
      Two compounding problems make this failure silent rather than visibly broken: (1)
      `holdover.noteSampleReceived()` (`teensy-ntp.ino`, right after `discipline.process()`) fires
      unconditionally regardless of accept/reject, so holdover's staleness timer never trips --
      no `inHoldover`, no growing dispersion, no stratum-16 fallback, even though the served clock
      is no longer being disciplined at all. (2) `WebContent::setPPSData()` (called before the
      accept/reject decision) echoes the raw *incoming* `gpstime` regardless of whether
      `ClockDiscipline` accepted it, so the web UI's "NTP time" field keeps advancing normally --
      confirmed on the bench (`NTP time: 2 (1900-01-01T00:00:02.000Z)`, i.e. displaying the
      correct post-wrap second) even while the actual disciplined `localClock` is silently frozen
      at its last pre-wrap ppb, free-running forever with no correction and no alarm visible
      anywhere. All three should probably be addressed together.
- [x] Section 6c (leap-second stall) as designed in `TESTPLAN.md`/`mitm/generate.py` can't run
      against normally-compiled (2026+) firmware at all: all four fixture samples came back `B`
      (bad clock), rejected by `gps_serial_poll()`'s `secondstime() < compileSecondsTime` guard
      before ever reaching `ClockDiscipline` -- that guard is correctly doing its job of rejecting
      a GPS date older than the firmware's own build time, which the fixture's 2016/2017 dates
      always are. Retested with a temporary build (`compileTime` seeded from a hardcoded 2016-01-01
      instead of `__DATE__`) to get past the guard -- see the next item for what that run found.
- [ ] **A real leap second produces one sample with a genuinely anomalous offset, which only
      corrupts anything if it lands during `ClockPID`'s bootstrap phase (no reboot-timing luck
      needed in the other case).** First observed via the 6c retest with a 60s lead-in: the stall
      correction (`ClockDiscipline.cpp:60-67`) fired correctly (`L 3692217636`) and the very next
      sample resolved clean, but the *following* two samples both showed `offsetHuman` at almost
      exactly `1.0` (`teensy-ntp.ino:221`), `dChiSq=ovf`, and `ppb` clamped to `NTPClock.cpp:57`'s
      `±500000` safety limit -- matching the same corruption signature as the buffer-never-reset
      issue above. **Revised after a follow-up retest with a 180s lead-in** (long enough to clear
      `ClockPID`'s 16-sample bootstrap before the leap instant): every sample, including the very
      first one after the `L` correction, stayed completely clean (tiny offsets, `dChiSq` back in
      its normal 36-70 range, `ppb` ~9958-9990, no clamping) -- ruling out "`localClock` is
      permanently behind after every leap second" as the mechanism, since that would corrupt every
      subsequent sample forever, not just transiently.
      Actual mechanism, traced through `ClockDiscipline::process()`: the leap-corrected sample's
      offset genuinely is anomalous for that one instant (nothing steps `localClock`'s own
      internal time across the inserted second -- it only ever gets frequency corrections via
      `setPpb()` after its initial `setTime()`, which can't absorb a discrete 1s event). Whether
      that one anomalous sample reaches `ClockPID`'s regression depends entirely on whether
      median-of-3 outlier rejection is active: during bootstrap, `pid_->full()` is false, so
      `ClockDiscipline` always takes the immediate-resolve branch with `median_index = wait_` --
      the actual `median(samples_[0..2])` call only happens when `wait_ == 0`, which bootstrap
      never reaches (`wait_` gets reset to `DISCIPLINE_WAIT_COUNT-1` every call in that regime), so
      the anomalous sample goes straight into the regression unfiltered. In steady state (buffer
      already full, as in the 180s-lead-in run), the same anomalous sample gets compared against
      two normal neighbors, loses the median comparison, and never reaches `ClockPID` at all --
      explaining the clean result.
      Practical implication: this is really a narrow window, not "every leap second" -- only a
      device that reboots (or otherwise resets `ClockPID`'s buffer) shortly before a real leap
      second would hit it; a device that's been running normally for even a few minutes beforehand
      should be unaffected, consistent with the 180s-lead-in result. Two independent fixes worth
      considering: make `ClockDiscipline` explicitly step `localClock` forward across a leap
      correction so no anomalous sample is generated in the first place (more robust than relying
      on median-of-3 timing luck), and/or extend outlier protection to cover the bootstrap phase
      too.
      Also confirmed working correctly in both retests: the NTP Leap Indicator field -- `+1s (64)`
      correctly announced before the transition, cleanly clearing to `(0)` afterward, with
      continuously correct served timestamps throughout.
