# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Design / future work

- [ ] Web UI shows 1900-ish dates instead of the real date after a real Y2036 wraparound.
      `index_js.h`'s date-formatting spots (`gotData()`'s `time`/`gpstimeHuman`, `gpsReportedTime`/
      `gpsReportedTimeStatus`, `holdoverStartTime`/`holdoverStartTimeHuman` -- all three do
      `new Date((json.<field> - 2208988800) * 1000)`) just subtract the NTP/Unix epoch offset from
      the raw wire-format seconds-since-1900 value with no awareness that a small value might be a
      *wrapped* post-2036 time rather than a literal near-1900 one. Confirmed on the bench
      (`data/run7`/`data/run8`): once the wire format wraps, these fields correctly show a small
      raw number but render as `(1900-01-01T00:0X:XXZ)` next to it, which is technically an
      accurate rendering of the raw (wrapped) value but reads as obviously wrong/confusing on a
      device that's actually running in 2036+. Unlike the four hazards in TESTPLAN.md section 7,
      this is purely a display issue -- the served NTP time/protocol behavior itself is correct
      (that's the wire format's own inherent Y2036 limit, not a bug); only the human-readable
      rendering needs fixing, e.g. by having the JS assume the most recent wrap epoch when the raw
      value looks implausibly small instead of always subtracting straight from 1900.

- [ ] `LeapSeconds.cpp`'s `leapSecondPendingToday()` has the same non-wraparound-safe `<=`-style
      comparison as `leapSecondOffsetAt()`/`leapSecondOffsetAtTai()` (now fixed, see DONE.md), but
      much lower practical stakes: it only misbehaves if a leap second happens to be scheduled
      exactly within the ~86400s window straddling the 2036-02-07 06:28:16 UTC wrap itself, not
      "forever after" like the other two. Deliberately left as-is for now; fix the same way
      (`elapsedWithin()`) if/when a leap second is ever scheduled near a future wraparound.
      (`leapSecondStallSecond()` uses pure equality comparison and is already wraparound-safe --
      no fix needed there.)

- [ ] `NTPClients` (NTPClients.cpp) does an O(n) linear scan over all 100 client slots on every
      packet (`addRx`/`addTx`/`findClient`/`expireClients`). Fine at n=100, but note if
      `NUMCLIENTS` ever grows significantly.
