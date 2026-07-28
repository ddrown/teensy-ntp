# TODO

Notes from a code review, roughly ordered by priority. Completed items have been moved to
`DONE.md`.

## Design / future work

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
