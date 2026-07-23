#!/usr/bin/env python3
"""Generates fixture files for TESTPLAN.md sections 6a/6b/6c -- see
player.py to play them back, and TODO.md's "NMEA MITM tooling" for the
overall design. Section 7a/7c (Y2036 wraparound) isn't a finite fixture --
see rebase_relay.py instead.

6a/6b assume the device is *already* disciplined on real GPS time before you
switch its GPS_SERIAL input over to this rig (see TESTPLAN.md's isolation
caveat: keep it off any network with real clients while doing this, and
reboot afterward rather than switching back to real NMEA). Their first fix
needs to land close to the real current time or the monotonicity guard will
just reject it as backwards, so --start defaults to a few seconds from now.

6c requires the opposite: power-cycle the Teensy (or otherwise get it right
before first GPS lock) immediately before playing that fixture, so its
first-ever sample takes the unconditional clock-set path.

Usage:
  ./generate.py 6a [--sentence-type zda|rmc] [--start ISO8601]
  ./generate.py 6b [--sentence-type zda|rmc] [--start ISO8601]
  ./generate.py 6c [--sentence-type zda|rmc]
"""

import argparse
from datetime import datetime, timedelta, timezone
from pathlib import Path

import nmea

FIXTURES_DIR = Path(__file__).parent / "fixtures"

SCENARIO_NOTES = {
    "6a": "# scenario 6a (spurious duplicate D): play against an ALREADY-SYNCED device.\n",
    "6b": "# scenario 6b (backwards D): play against an ALREADY-SYNCED device.\n",
    "6c": "# scenario 6c (leap-second stall L): POWER-CYCLE the Teensy immediately\n"
          "# before playing this fixture -- the first group must take the\n"
          "# unconditional clock-set path.\n",
}


def write_fixture(name, groups):
    FIXTURES_DIR.mkdir(exist_ok=True)
    path = FIXTURES_DIR / f"{name}.nmea"
    with open(path, "w") as f:
        f.write(SCENARIO_NOTES[name])
        for i, group in enumerate(groups):
            if i > 0:
                f.write("---\n")
            for sentence in group:
                f.write(sentence + "\n")
    print(f"wrote {path} ({len(groups)} groups)")


def scenario_6a(sentence_type, start):
    """Spurious duplicate D: hold the seconds field static for one extra
    fix on an ordinary day, surrounded by normal fixes so the D is clearly
    isolated and telemetry can be checked before/after."""
    times = [
        start,
        start + timedelta(seconds=1),
        start + timedelta(seconds=1),  # the duplicate
        start + timedelta(seconds=2),
        start + timedelta(seconds=3),
    ]
    return [nmea.fix_group(t, sentence_type) for t in times]


def scenario_6b(sentence_type, start):
    """Backwards D: emit a timestamp a few seconds earlier than the last
    accepted one, surrounded by normal fixes."""
    times = [
        start,
        start + timedelta(seconds=1),
        start - timedelta(seconds=3),  # the backwards jump
        start + timedelta(seconds=2),
        start + timedelta(seconds=3),
    ]
    return [nmea.fix_group(t, sentence_type) for t in times]


def scenario_6c(sentence_type):
    """Leap-second stall L: the first-ever fix after a reset lands exactly
    on HH:MM:59 the day before a compiled leap-second date -- LeapSeconds.cpp's
    last table entry is effective 2017-01-01, so the leap second is
    2016-12-31 23:59:60 and this is the instant just before it. Repeating
    that identical timestamp triggers the stall correction (L, steps
    forward to 2017-01-01 00:00:00); a couple of normal fixes afterward
    confirm telemetry recovers cleanly."""
    eve = datetime(2016, 12, 31, 23, 59, 59, tzinfo=timezone.utc)
    times = [
        eve,
        eve,  # repeated -- triggers the stall correction
        datetime(2017, 1, 1, 0, 0, 1, tzinfo=timezone.utc),
        datetime(2017, 1, 1, 0, 0, 2, tzinfo=timezone.utc),
    ]
    return [nmea.fix_group(t, sentence_type) for t in times]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", choices=["6a", "6b", "6c"])
    ap.add_argument("--sentence-type", choices=["zda", "rmc"], default="zda",
                     help="match whichever this GPS module actually emits (default: zda)")
    ap.add_argument("--start", type=datetime.fromisoformat,
                     help="ISO8601 UTC start time for 6a/6b (default: 30s from now, "
                          "to leave time to start the player)")
    args = ap.parse_args()

    if args.scenario in ("6a", "6b"):
        start = args.start or (datetime.now(timezone.utc) + timedelta(seconds=30))
        builder = scenario_6a if args.scenario == "6a" else scenario_6b
        groups = builder(args.sentence_type, start)
    else:
        groups = scenario_6c(args.sentence_type)

    write_fixture(args.scenario, groups)


if __name__ == "__main__":
    main()
