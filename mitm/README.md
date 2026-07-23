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
./rebase_relay.py --port /dev/ttyUSB0 --baud <whatever GPS_SERIAL is running at>
```

Leave it running through the wrap for section 7b/7d, and for as long as needed afterward (up to
~4096s+) for section 7c's client-expiry soak.

## `--sentence-type`

Both `generate.py` and `rebase_relay.py` take `--sentence-type zda|rmc` (default `zda`) -- match
whichever this GPS module actually emits (`GPS.cpp`'s own detection is runtime-based, not a
compile-time choice, so the fixture needs to match it too).
