# NMEA MITM test rig

Tooling for `TESTPLAN.md` sections 6 (duplicate/backwards/leap-second-stall timestamps) and 7
(Y2036 wire-timestamp rollover), which otherwise need a decade of waiting for a real leap second
or wraparound. See `TODO.md`, "NMEA MITM tooling", for the design rationale.

Hardware: leave the GPS module's PPS output wired to the Teensy as normal, but disconnect its
NMEA TX line. Wire a single USB-UART adapter's TX to the Teensy's `GPS_SERIAL` RX pin instead,
driven by the scripts here. The physical PPS line stays real and untouched throughout.

```
pip install -r requirements.txt
```

## Sections 6a/6b/6c (finite fixtures)

```
./generate.py 6a   # or 6b, 6c -- writes fixtures/<scenario>.nmea
./player.py --port /dev/ttyUSB0 --baud <whatever GPS_SERIAL is running at> fixtures/6a.nmea
```

- **6a/6b** assume the device is *already* disciplined on real GPS time -- switch its
  `GPS_SERIAL` input over to this rig without power-cycling. `generate.py` defaults `--start` to
  30 seconds from now for this reason: the first fix needs to land close to real current time or
  the monotonicity guard just rejects it as backwards before the test even starts.
- **6c** requires the opposite: power-cycle the Teensy (or otherwise get it right before first
  GPS lock) immediately before playing that fixture, so its first-ever sample takes the
  unconditional clock-set path.
- Per `TESTPLAN.md`'s isolation caveat: keep the device off any network with real clients while
  running these, and reboot afterward rather than switching back to real NMEA and hoping the
  regression buffer/PID state recovers cleanly.

## Section 7a/7c (Y2036 wraparound)

Open-ended, not a fixture file -- see `rebase_relay.py`'s docstring. Power-cycle the Teensy first,
then:

```
./rebase_relay.py wrap --port /dev/ttyUSB0 --baud <whatever GPS_SERIAL is running at>
```

Leave it running through the wrap for section 7b/7d, and for as long as needed afterward (up to
~4096s+) for section 7c's client-expiry soak.

## Section 6c (leap-second handling), extended

`generate.py 6c`'s fixture only gives ~2 seconds of margin around the leap instant. For more
lead-in/observation time, or to test how the firmware handles a receiver reporting the leap second
differently, use `rebase_relay.py leap` instead -- open-ended like `wrap`, power-cycle first:

```
./rebase_relay.py leap --port /dev/ttyUSB0 --baud <baud> \
    --leap-at 2016-12-31T23:59:59 --leap-mode dup59 --lead-seconds 60
```

`--leap-mode`:
- `dup59` -- repeats the last regular second an extra time before advancing (a receiver that
  stalls instead of emitting `:60`). What `generate.py 6c` already tests; `L` expected.
- `add60` -- sends a literal `HH:MM:60` sentence at the transition. `DateTime`'s own leap-second
  arithmetic handles this natively; no `D`/`L` expected. Untested against real hardware until this
  mode existed.
- `dup00` -- repeats the second *after* the leap instead of the one before.
  `leapSecondStallSecond()` only recognizes a duplicate of the second *before* a compiled
  `LeapSeconds.cpp` entry, so this exercises the plain duplicate-rejection path (`D`), not leap
  recognition.

`--leap-at` must be exactly one second before a real `LeapSeconds.cpp` table entry for `dup59`/
`add60` to mean anything to the firmware -- currently only `2016-12-31T23:59:59` (effective
`2017-01-01`) exists in the compiled table. Note `gps_serial_poll()`'s `compileSecondsTime` sanity
guard rejects any GPS date older than the firmware's own build time, so testing against that real
date needs a firmware build with `compileTime` overridden to something before it (see the
"leap-second-stall correction never steps `localClock`" TODO.md item for how that was done).

## `--sentence-type`

`generate.py` and `rebase_relay.py` take `--sentence-type zda|rmc` (default `zda`) -- match
whichever this GPS module actually emits (`GPS.cpp`'s own detection is runtime-based, not a
compile-time choice, so the fixture needs to match it too).
