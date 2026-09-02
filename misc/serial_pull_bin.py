#!/usr/bin/env python3
"""Retrieve one arbitrary SD-relative file from PocketSSH over local serial."""

from __future__ import annotations

import argparse
import pathlib
import re
import time
import zlib

try:
    import serial  # type: ignore
except ImportError as exc:
    raise SystemExit("pyserial is required; use the ESP-IDF Python environment") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--remote-name", required=True, help="SD-relative source path")
    parser.add_argument("--output", required=True, help="Local destination, written only after CRC verification")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--pre-wait",
        type=float,
        default=3.5,
        help="Seconds to wait after opening USB serial before sending the request",
    )
    args = parser.parse_args()

    output = pathlib.Path(args.output)
    temporary = output.with_name(output.name + ".partial")
    received = bytearray()
    begin = None
    pending = ""
    deadline = time.time() + max(1.0, args.timeout)
    begin_re = re.compile(r"POCKETCTL serialtx_begin path=.* bytes=(\d+) crc=([0-9a-fA-F]{8})")
    data_re = re.compile(r"POCKETCTL serialtx_data ([0-9a-fA-F]+)")
    complete_re = re.compile(r"POCKETCTL serialtx_complete path=.* bytes=(\d+) crc=([0-9a-fA-F]{8})")

    # Set control lines before opening.  On USB-Serial/JTAG hardware, changing
    # them after open can reset the target a second time and lose the request.
    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 0.1
    ser.write_timeout = 5
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        # USB-Serial/JTAG opening resets the T-Pager.  Wait until its PocketSSH
        # control task is running before sending the first (otherwise lost)
        # request.  The subsequent timeout remains for the framed response.
        time.sleep(max(0.0, args.pre_wait))
        ser.reset_input_buffer()
        ser.write(f"__pocketctl cmd serialtx {args.remote_name}\n".encode("utf-8"))
        ser.flush()
        while time.time() < deadline:
            text = ser.read(4096).decode(errors="ignore")
            if not text:
                continue
            pending += text
            while "\n" in pending:
                line, pending = pending.split("\n", 1)
                if begin is None:
                    match = begin_re.search(line)
                    if match:
                        begin = (int(match.group(1)), int(match.group(2), 16))
                        print(f"Receiving {begin[0]} bytes from {args.remote_name}")
                match = data_re.search(line)
                if match:
                    try:
                        received.extend(bytes.fromhex(match.group(1)))
                    except ValueError:
                        raise SystemExit("Malformed serialtx data frame")
                match = complete_re.search(line)
                if match and begin is not None:
                    expected_size, expected_crc = begin
                    if int(match.group(1)) != expected_size or int(match.group(2), 16) != expected_crc:
                        raise SystemExit("Device serialtx completion metadata changed")
                    actual_crc = zlib.crc32(received) & 0xFFFFFFFF
                    if len(received) != expected_size or actual_crc != expected_crc:
                        raise SystemExit(f"CRC verification failed: bytes={len(received)} crc={actual_crc:08x}")
                    temporary.write_bytes(received)
                    temporary.replace(output)
                    print(f"Verified {len(received)} bytes CRC32 {actual_crc:08x}: {output}")
                    return 0
    finally:
        ser.close()
    raise SystemExit("Timed out waiting for verified serialtx completion")


if __name__ == "__main__":
    raise SystemExit(main())
