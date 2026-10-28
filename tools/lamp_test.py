#!/usr/bin/env python3
"""Lights one group of bits of the LCM light job for a few seconds, then always ends the job.

Run:  python3 tools/lamp_test.py <data byte 0-11, not 9/10> <mask hex> [seconds]   e.g. 0 FF  or  7 2C 8
Add --dry to only print what would be sent.
"""
import argparse
import math
import subprocess
import sys
from pathlib import Path

PORT = "/dev/cu.usbserial-10"
REFRESH_SECONDS = 2.5          # the LCM drops the job after a few seconds, so it is sent again
DATA_BYTES = 12
FORBIDDEN_BYTES = (9, 10)      # dimmer and rear load, echoed by the LCM


def build_frame(byte_index, mask):
    data = [0] * DATA_BYTES
    data[byte_index] = mask
    return "r D0 0C " + " ".join(f"{value:02X}" for value in data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("byte", type=int)
    parser.add_argument("mask", help="hex")
    parser.add_argument("seconds", type=float, nargs="?", default=7.5)
    parser.add_argument("--dry", action="store_true")
    args = parser.parse_args()

    mask = int(args.mask, 16)
    if not 0 <= args.byte < DATA_BYTES or args.byte in FORBIDDEN_BYTES or not 0 < mask <= 0xFF:
        sys.exit("byte must be 0-11 (not 9 or 10) and mask 01-FF")

    frame = build_frame(args.byte, mask)
    repeats = max(1, math.ceil(args.seconds / REFRESH_SECONDS))
    sends = ["tx on"] + [frame] * repeats
    total_seconds = 1 + REFRESH_SECONDS * len(sends) + 0.5

    command = [sys.executable, "-I", str(Path(__file__).with_name("serial_log.py")), PORT, str(total_seconds),
               "--first", "1", "--gap", str(REFRESH_SECONDS)]
    for line in sends:
        command += ["--send", line]
    for line in ("r D0 9F", "tx off"):
        command += ["--final", line]

    if args.dry:
        print(" ".join(f'"{part}"' if " " in part else part for part in command))
        return
    subprocess.run(command)


main()
