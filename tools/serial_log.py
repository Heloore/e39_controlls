#!/usr/bin/env python3
"""Reads the ESP32 serial output for a while, optionally rebooting the board and sending lines. No extra packages.

Run:  python3 tools/serial_log.py /dev/cu.usbserial-10 20 --reset --send wifi --send "wifi on"
Prints every line with the seconds since start. --reset pulses EN (RTS) first, so the boot log is captured.
--send lines go out one after another, --gap seconds apart (the first after --first seconds).
--final lines are always sent at the end, also after Ctrl-C or a kill, so a test never leaves a job running.
"""
import argparse
import fcntl
import os
import select
import signal
import termios
import time

DTR = termios.TIOCM_DTR
RTS = termios.TIOCM_RTS
BAUD = termios.B115200


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0                                               # input flags
    attributes[1] = 0                                               # output flags
    attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL    # 8N1, no flow control
    attributes[3] = 0                                               # raw
    attributes[4] = attributes[5] = BAUD
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    return fd


def set_line(fd, line, on):
    request = termios.TIOCMBIS if on else termios.TIOCMBIC
    fcntl.ioctl(fd, request, line.to_bytes(4, "little"))


def reset_board(fd):
    set_line(fd, DTR, False)
    set_line(fd, RTS, True)
    time.sleep(0.15)
    set_line(fd, RTS, False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("seconds", type=float)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--send", action="append", default=[])
    parser.add_argument("--first", type=float, default=3.0)
    parser.add_argument("--gap", type=float, default=2.0)
    parser.add_argument("--final", action="append", default=[])
    args = parser.parse_args()

    def interrupted(*_):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)

    fd = open_port(args.port)
    if args.reset:
        reset_board(fd)

    start = time.time()
    pending = [(args.first + index * args.gap, text) for index, text in enumerate(args.send)]
    buffer = b""

    try:
        while time.time() - start < args.seconds:
            elapsed = time.time() - start
            while pending and elapsed >= pending[0][0]:
                _, text = pending.pop(0)
                print(f"[{elapsed:6.2f}] >>> {text}", flush=True)
                os.write(fd, text.encode() + b"\n")

            if select.select([fd], [], [], 0.05)[0]:
                buffer += os.read(fd, 4096)
                while b"\n" in buffer:
                    raw, buffer = buffer.split(b"\n", 1)
                    print(f"[{time.time() - start:6.2f}] {raw.decode(errors='replace').rstrip()}", flush=True)
    except KeyboardInterrupt:
        print("interrupted", flush=True)
    finally:
        for text in args.final:
            print(f"[{time.time() - start:6.2f}] >>> {text} (final)", flush=True)
            os.write(fd, text.encode() + b"\n")
            time.sleep(0.2)
        if args.final:
            time.sleep(0.6)
            while select.select([fd], [], [], 0)[0]:
                buffer += os.read(fd, 4096)
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                print(f"[{time.time() - start:6.2f}] {raw.decode(errors='replace').rstrip()}", flush=True)
        os.close(fd)

    if buffer:
        print(f"[{time.time() - start:6.2f}] {buffer.decode(errors='replace').rstrip()}", flush=True)


main()
