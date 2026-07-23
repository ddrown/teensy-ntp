#!/usr/bin/env python3
"""Fixture-file player for the NMEA MITM test rig (TESTPLAN.md sections 6a/6b/6c).

Reads a fixture file (see generate.py) of NMEA sentence groups separated by
'---' marker lines, and sends one group per real second, at a configured
millisecond offset from the top of the second -- comfortably inside the
950ms PPS-to-NMEA lag tolerance (see updateTime() in teensy-ntp.ino), since
real GPS PPS edges land almost exactly on the UTC second boundary and this
just needs to track wall-clock time, not the real PPS signal directly.
Exits once the file is exhausted.

Section 7a/7c (Y2036 wraparound) isn't a finite fixture -- see
rebase_relay.py instead.

Usage:
  ./player.py --port /dev/ttyUSB0 --baud 9600 fixtures/6a.nmea
"""

import argparse
import time

import serial


def load_groups(path):
    groups = [[]]
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line == "---":
                groups.append([])
                continue
            groups[-1].append(line)
    return [g for g in groups if g]


def wait_until_offset(offset_ms):
    """Sleep until the next moment that is `offset_ms` past a second
    boundary -- later this second if that point hasn't passed yet,
    otherwise next second."""
    now = time.time()
    target = int(now) + offset_ms / 1000.0
    if target <= now:
        target += 1.0
    time.sleep(max(0, target - time.time()))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="serial device wired to the Teensy's GPS_SERIAL RX")
    ap.add_argument("--baud", type=int, required=True, help="must match GPS_SERIAL's detected/configured baud")
    ap.add_argument("--offset-ms", type=int, default=200,
                     help="milliseconds after the top of the second to send each group (default: 200)")
    ap.add_argument("fixture", help="path to a fixture file from generate.py")
    args = ap.parse_args()

    groups = load_groups(args.fixture)
    print(f"loaded {len(groups)} group(s) from {args.fixture}")

    with serial.Serial(args.port, args.baud) as ser:
        for i, group in enumerate(groups):
            wait_until_offset(args.offset_ms)
            for sentence in group:
                ser.write((sentence + "\r\n").encode("ascii"))
            print(f"[{i + 1}/{len(groups)}] {group[0]}")

    print("done")


if __name__ == "__main__":
    main()
