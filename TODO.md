# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Design / future work

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.

## Holdover bench session findings (2026-07-22)

From a real PPS-disconnect holdover test (see `holdover.txt`), roughly in priority order.

- [ ] GPS satellite strong/weak signal counts sometimes jump abruptly (e.g. 5→39) -- observed
      during the 2026-07-22 holdover bench session, not yet investigated.
- [ ] Web UI: "Holdover elapsed" could show elapsed time since the last real PPS pulse instead (or
      in addition), so it lines up directly against "Reference time" for a reader comparing the two.
- [ ] Web UI: the flat `<p>` list of stats at the bottom of the status page is hard to scan --
      consider reorganizing into a table.
- [ ] Web UI: show the raw date/time actually coming from the GPS module's NMEA sentences
      somewhere on the page, distinct from the served "NTP time" field (which now always reads
      `localClock` -- see DONE.md, "WebContent gpstime freeze during holdover"). After a cold
      start, a valid GPS-reported date is the second sign of life the module gives (after
      satellites-in-view counts start climbing, before PPS/lock are fully established), so this is
      a useful "is it making progress yet" signal an operator would otherwise have no way to see
      from the web UI at all.

## MITM bench session follow-up (2026-07-23)

- [ ] Even with the Y2036 monotonicity fix (see DONE.md, "Y2036 wraparound in ClockDiscipline's
      monotonicity guard"), one compounding visibility gap identified alongside it is still open:
      `holdover.noteSampleReceived()` (`teensy-ntp.ino`, right after `discipline.process()`) fires
      unconditionally regardless of accept/reject, so a sustained run of rejected samples (for any
      reason, not just the now-fixed wraparound) never trips holdover's staleness timer -- no
      `inHoldover`, no growing dispersion, no stratum-16 fallback, even though the served clock may
      no longer be getting disciplined at all. (The other half originally identified alongside this
      -- `WebContent` echoing an unaccepted raw `gpstime` -- is now moot: see DONE.md, "WebContent
      gpstime freeze during holdover," `WebContent` no longer tracks a GPS-derived `gpstime` at
      all.)
