#!/usr/bin/env python3
"""Open-ended live NMEA relays for TESTPLAN.md sections 6c/7 -- for scenarios
that can't be finite pre-generated fixtures (see generate.py/player.py for
those). Both modes require a power-cycled Teensy right before starting: the
first sample sent must take the unconditional clock-set path (the very
first sample after a reset), or a historical/rebased timestamp just looks
like an ordinary backwards jump and gets rejected instead of exercising
anything.

Two modes:

  wrap  -- section 7 (Y2036 wire-timestamp rollover). Seeds the clock a
           configurable lead time before the 2036-02-07 06:28:16 UTC wrap,
           then relays indefinitely with every date/time field rebased by
           a fixed offset, so the actual wrap crosses within minutes of
           real time and section 7c's ~4096s client-expiry window can be
           soaked for as long as needed afterward.

  leap  -- section 6c (leap-second handling), generalized. generate.py's
           6c fixture only gives ~2 seconds of margin around the leap
           instant; this gives a configurable lead-in and then keeps
           running indefinitely afterward, and supports three ways a real
           receiver might report the same leap second:
             dup59  -- repeat the last regular second an extra time before
                       advancing (the "receiver stalls instead of emitting
                       :60" case -- GPS.cpp/ClockDiscipline's stall
                       detection, prints L). This is what generate.py's 6c
                       fixture already tests, just with more margin here.
             add60  -- send a literal HH:MM:60 sentence at the transition.
                       DateTime's own leap-second arithmetic handles this
                       natively (no D/L expected) -- untested against real
                       hardware until this mode existed.
             dup00  -- repeat the second *after* the leap instead of the
                       one before. leapSecondStallSecond() only recognizes
                       a duplicate of the second *before* a compiled
                       LeapSeconds.cpp entry, so this exercises the plain
                       duplicate-rejection path (D), not leap recognition.
           --leap-at must be exactly one second before a real
           LeapSeconds.cpp table entry for dup59/add60 to mean anything to
           the firmware -- currently only 2016-12-31T23:59:59 (effective
           2017-01-01) exists in the compiled table.

Usage:
  ./rebase_relay.py wrap --port /dev/ttyUSB0 --baud 9600 --lead-seconds 180
  ./rebase_relay.py leap --port /dev/ttyUSB0 --baud 9600 \\
      --leap-at 2016-12-31T23:59:59 --leap-mode dup59 --lead-seconds 60
"""

import argparse
import time
from datetime import datetime, timedelta, timezone

import serial

import nmea

WRAP_INSTANT = datetime(2036, 2, 7, 6, 28, 16, tzinfo=timezone.utc)


def wait_until_offset(offset_ms):
    now = time.time()
    target = int(now) + offset_ms / 1000.0
    if target <= now:
        target += 1.0
    time.sleep(max(0, target - time.time()))


def run_wrap(ser, args):
    seed_time = WRAP_INSTANT - timedelta(seconds=args.lead_seconds)
    offset = seed_time - datetime.now(timezone.utc)
    print(f"rebase offset: {offset} (first fix will read {seed_time.isoformat()})")
    print("make sure the Teensy was just power-cycled -- Ctrl-C to stop")

    count = 0
    while True:
        wait_until_offset(args.offset_ms)
        fabricated = datetime.now(timezone.utc) + offset
        for sentence in nmea.fix_group(fabricated, args.sentence_type):
            ser.write((sentence + "\r\n").encode("ascii"))
        count += 1
        if count % 60 == 0:
            print(f"sent {count} fixes, now at {fabricated.isoformat()}")


def leap_sequence(leap_at, lead_seconds, mode):
    """Yields (calendar_time, is_literal_60) pairs: lead_seconds of normal
    fixes up to leap_at, the mode-specific transition, then normal fixes
    forever after."""
    t = leap_at - timedelta(seconds=lead_seconds)
    while t < leap_at:
        yield (t, False)
        t += timedelta(seconds=1)

    if mode == "dup59":
        yield (leap_at, False)  # ordinary send of the last regular second
        yield (leap_at, False)  # repeated -- triggers the stall correction
        t = leap_at + timedelta(seconds=1)
    elif mode == "add60":
        yield (leap_at, True)   # literal HH:MM:60
        t = leap_at + timedelta(seconds=1)
    elif mode == "dup00":
        yield (leap_at, False)
        after = leap_at + timedelta(seconds=1)
        yield (after, False)
        yield (after, False)    # repeated -- ordinary duplicate, not leap-recognized
        t = after + timedelta(seconds=1)
    else:
        raise ValueError(f"unknown leap mode {mode!r}")

    while True:
        yield (t, False)
        t += timedelta(seconds=1)


def run_leap(ser, args):
    print(f"leap-second instant: {args.leap_at.isoformat()}, mode={args.leap_mode}, "
          f"{args.lead_seconds}s lead-in")
    print("make sure the Teensy was just power-cycled -- Ctrl-C to stop")

    count = 0
    for calendar_time, is_literal_60 in leap_sequence(args.leap_at, args.lead_seconds, args.leap_mode):
        wait_until_offset(args.offset_ms)
        sentences = (nmea.leap_second_group(calendar_time, args.sentence_type) if is_literal_60
                     else nmea.fix_group(calendar_time, args.sentence_type))
        for sentence in sentences:
            ser.write((sentence + "\r\n").encode("ascii"))

        count += 1
        near_transition = abs((calendar_time - args.leap_at).total_seconds()) <= 1
        if count % 30 == 0 or near_transition:
            label = "LEAP " if is_literal_60 else ""
            print(f"[{count}] {label}sent {calendar_time.isoformat()}")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--port", required=True)
    common.add_argument("--baud", type=int, required=True)
    common.add_argument("--offset-ms", type=int, default=200)
    common.add_argument("--sentence-type", choices=["zda", "rmc"], default="zda")

    sub = ap.add_subparsers(dest="mode", required=True)

    wrap_p = sub.add_parser("wrap", parents=[common], help="section 7: Y2036 wraparound")
    wrap_p.add_argument("--lead-seconds", type=int, default=180,
                         help="seconds before the wrap the first sent fix should land (default: 180)")

    leap_p = sub.add_parser("leap", parents=[common], help="section 6c: leap-second handling")
    leap_p.add_argument("--leap-at", type=datetime.fromisoformat, required=True,
                         help="ISO8601 UTC instant of the last normal second before the leap "
                              "(e.g. 2016-12-31T23:59:59) -- must be a real LeapSeconds.cpp "
                              "table entry minus 1s for dup59/add60 to mean anything")
    leap_p.add_argument("--leap-mode", choices=["dup59", "add60", "dup00"], default="dup59")
    leap_p.add_argument("--lead-seconds", type=int, default=60,
                         help="seconds of normal traffic before the leap instant (default: 60)")

    args = ap.parse_args()
    if args.mode == "leap" and args.leap_at.tzinfo is None:
        args.leap_at = args.leap_at.replace(tzinfo=timezone.utc)

    with serial.Serial(args.port, args.baud) as ser:
        if args.mode == "wrap":
            run_wrap(ser, args)
        else:
            run_leap(ser, args)


if __name__ == "__main__":
    main()
