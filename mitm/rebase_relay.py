#!/usr/bin/env python3
"""Open-ended live NMEA relay for TESTPLAN.md section 7 (Y2036 wraparound).

Unlike 6a/6b/6c (finite, pre-generated fixtures -- see generate.py/player.py),
section 7a is inherently open-ended: seed the device's clock a few minutes
before the 2036-02-07 06:28:16 UTC wire-timestamp wrap, then keep relaying
NMEA traffic indefinitely with every date/time field rebased by that same
fixed offset -- so the actual wrap crosses within minutes of real time, and
section 7c's ~4096s client-expiry window can be soaked for as long as needed
afterward just by leaving this running (there's no way to speed that part up:
PPS keeps ticking in real time regardless, since we're only replacing the
NMEA text feed, not the physical PPS line).

Power-cycle the Teensy (or otherwise get it to right before first GPS lock)
immediately before starting this script, so its first-ever sample takes the
unconditional clock-set path and adopts the seeded pre-wrap time exactly.
Do not restart this script mid-run -- re-seeding partway through would look
like a backwards jump (section 6b's guard) rather than a clean wrap crossing.

Usage:
  ./rebase_relay.py --port /dev/ttyUSB0 --baud 9600 --lead-seconds 180
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


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, required=True)
    ap.add_argument("--offset-ms", type=int, default=200)
    ap.add_argument("--lead-seconds", type=int, default=180,
                     help="how many seconds before the Y2036 wrap the first sent fix "
                          "should land (default: 180)")
    ap.add_argument("--sentence-type", choices=["zda", "rmc"], default="zda")
    args = ap.parse_args()

    seed_time = WRAP_INSTANT - timedelta(seconds=args.lead_seconds)
    offset = seed_time - datetime.now(timezone.utc)
    print(f"rebase offset: {offset} (first fix will read {seed_time.isoformat()})")
    print("make sure the Teensy was just power-cycled -- Ctrl-C to stop")

    with serial.Serial(args.port, args.baud) as ser:
        count = 0
        while True:
            wait_until_offset(args.offset_ms)
            fabricated = datetime.now(timezone.utc) + offset
            for sentence in nmea.fix_group(fabricated, args.sentence_type):
                ser.write((sentence + "\r\n").encode("ascii"))
            count += 1
            if count % 60 == 0:
                print(f"sent {count} fixes, now at {fabricated.isoformat()}")


if __name__ == "__main__":
    main()
