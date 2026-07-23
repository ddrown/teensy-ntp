# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.

## Holdover bench session findings (2026-07-22)

From a real PPS-disconnect holdover test (see `holdover.txt`), roughly in priority order.

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

## MITM bench session follow-up (2026-07-23)

- [ ] Even with the Y2036 monotonicity fix (see DONE.md, "Y2036 wraparound in ClockDiscipline's
      monotonicity guard"), two compounding visibility gaps identified alongside it are still open:
      `holdover.noteSampleReceived()` (`teensy-ntp.ino`, right after `discipline.process()`) fires
      unconditionally regardless of accept/reject, so a sustained run of rejected samples (for any
      reason, not just the now-fixed wraparound) never trips holdover's staleness timer -- no
      `inHoldover`, no growing dispersion, no stratum-16 fallback, even though the served clock may
      no longer be getting disciplined at all; and `WebContent::setPPSData()` (called before the
      accept/reject decision) echoes the raw *incoming* `gpstime` regardless of whether
      `ClockDiscipline` accepted it, so the web UI's "NTP time" field keeps advancing normally even
      while `localClock` is frozen. Both probably want fixing together.
