#!/usr/bin/env python3
"""Send PocketSSH control commands over a serial port and stream the response."""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit(
        "pyserial is required. Use the ESP-IDF Python environment, for example:\n"
        "/Users/sparky/.espressif/python_env/idf5.5_py3.14_env/bin/python "
        "misc/pocketctl_serial.py --port /dev/cu.usbmodem201201 --ping"
    ) from exc


def stream_until(ser: serial.Serial, deadline: float) -> None:
    while time.time() < deadline:
        data = ser.read(4096)
        if data:
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port for the T-Deck Plus")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--pre-wait", type=float, default=4.0, help="Seconds to read before sending")
    parser.add_argument("--read-seconds", type=float, default=12.0, help="Seconds to read after each sent line")
    parser.add_argument("--between-seconds", type=float, default=0.0, help="Extra idle seconds between sent lines")
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--ping", action="store_true", help="Send __pocketctl ping")
    action.add_argument("--cmd", action="append", help="Run a terminal command through __pocketctl cmd; may be repeated")
    action.add_argument("--raw", action="append", help="Send an exact control payload after __pocketctl; may be repeated")
    args = parser.parse_args()

    if args.ping:
        lines = ["__pocketctl ping\n"]
    elif args.cmd is not None:
        lines = [f"__pocketctl cmd {cmd}\n" for cmd in args.cmd]
    else:
        lines = [f"__pocketctl {raw}\n" for raw in args.raw]

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.1
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        stream_until(ser, time.time() + args.pre_wait)
        for index, line in enumerate(lines):
            if index > 0 and args.between_seconds > 0:
                stream_until(ser, time.time() + args.between_seconds)
            ser.write(line.encode("utf-8"))
            ser.flush()
            print(f"\n---sent {line.strip()}---", flush=True)
            stream_until(ser, time.time() + args.read_seconds)
    finally:
        ser.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
