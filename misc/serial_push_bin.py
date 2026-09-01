#!/usr/bin/env python3
"""
Push a local binary file to PocketSSH over USB serial using the `serialrx` protocol.

Device-side flow:
1) Boot PocketSSH on T-Pager.
2) Run this script from host.

Default behavior:
- Sends a control line (`__pocketctl serialrx <name>`) to trigger receiver mode.
- Waits for firmware readiness marker in serial logs.
- Streams BEGIN/DATA/END payload.
- Defaults to staging `/sdcard/PocketSSH-TPager.bin` on device.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
import time
import zlib

try:
    import serial  # type: ignore
except ImportError as exc:
    raise SystemExit("pyserial is required: pip install pyserial") from exc

SCRIPT_DIR = pathlib.Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent
ESP_ROOT = REPO_ROOT.parents[2]
DEFAULT_BUILD_BIN = ESP_ROOT / "_local" / "build" / "pocketssh" / "tpager" / "PocketSSH.bin"
DEFAULT_PACKAGED_BIN = ESP_ROOT / "_local" / "packages" / "pocketssh" / "PocketSSH-TPager.bin"
DEFAULT_TDECKPLUS_BIN = ESP_ROOT / "_local" / "packages" / "pocketssh" / "PocketSSH-2.0.bin"
DEFAULT_REMOTE_NAME = "PocketSSH-TPager.bin"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send binary to PocketSSH serialrx receiver")
    parser.add_argument("--port", required=True, help="Serial port (e.g. /dev/cu.usbmodem201101)")
    parser.add_argument(
        "--target",
        choices=("tpager", "tdeckplus"),
        default="tpager",
        help="Choose default binary and remote filename (default: tpager)",
    )
    parser.add_argument(
        "--file",
        default=str(DEFAULT_PACKAGED_BIN),
        help=f"Path to binary to send (default: {DEFAULT_PACKAGED_BIN})",
    )
    parser.add_argument(
        "--chunk-bytes",
        type=int,
        default=128,
        help="Bytes per DATA frame before hex encoding (default: 128)",
    )
    parser.add_argument(
        "--max-bytes",
        type=int,
        default=0,
        help="Persist at most this many payload bytes, then send PAUSE for resumable staging (default: complete file)",
    )
    parser.add_argument("--reset-partial", action="store_true",
                        help="Discard the device's resumable partial for this filename before staging")
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)",
    )
    parser.add_argument(
        "--tail-seconds",
        type=float,
        default=3.0,
        help="Seconds to read device output after transfer (default: 3)",
    )
    parser.add_argument(
        "--remote-name",
        default=DEFAULT_REMOTE_NAME,
        help=f"Destination filename on SD root (default: {DEFAULT_REMOTE_NAME})",
    )
    parser.add_argument(
        "--no-trigger",
        action="store_true",
        help="Do not send __pocketctl trigger; assume serialrx is already active",
    )
    parser.add_argument(
        "--trigger-timeout",
        type=float,
        default=10.0,
        help="Seconds to wait for serialrx readiness after trigger (default: 10)",
    )
    parser.add_argument(
        "--trigger-command",
        default="",
        help="Override control command (default: '__pocketctl serialrx <remote-name>')",
    )
    return parser.parse_args()


def wait_for_ready(ser: serial.Serial, timeout_s: float) -> int | None:
    deadline = time.time() + max(0.1, timeout_s)
    window = ""
    markers = ("pocketctl serialrx_ready", "serialrx ready:", "serialrx: waiting for begin")
    while time.time() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        text = chunk.decode(errors="ignore")
        sys.stdout.write(text)
        sys.stdout.flush()
        window = (window + text).lower()
        if len(window) > 4096:
            window = window[-4096:]
        if any(marker in window for marker in markers):
            match = re.search(r"pocketctl serialrx_ready[^\r\n]*partial=(\d+)", window)
            return int(match.group(1)) if match else 0
    return None


def wait_for_pause(ser: serial.Serial, timeout_s: float, expected_bytes: int, expected_total: int) -> bool:
    deadline = time.time() + max(0.1, timeout_s)
    window = ""
    while time.time() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        text = chunk.decode(errors="ignore")
        sys.stdout.write(text)
        sys.stdout.flush()
        window = (window + text).lower()
        if len(window) > 4096:
            window = window[-4096:]
        if ("pocketctl serialrx_paused" in window and f"bytes={expected_bytes}" in window and
                f"total={expected_total}" in window):
            return True
    return False


def wait_for_completion(ser: serial.Serial, timeout_s: float, expected_bytes: int, expected_crc32: int) -> bool:
    """Require device-side size and CRC evidence before accepting an SD copy."""
    deadline = time.time() + max(0.1, timeout_s)
    window = ""
    expected = f"pocketctl serialrx_complete"
    bytes_marker = f"bytes={expected_bytes}"
    crc_marker = f"crc={expected_crc32:08x}"
    while time.time() < deadline:
        chunk = ser.read(4096)
        if not chunk:
            continue
        text = chunk.decode(errors="ignore")
        sys.stdout.write(text)
        sys.stdout.flush()
        window = (window + text).lower()
        if len(window) > 4096:
            window = window[-4096:]
        if expected in window and bytes_marker in window and crc_marker in window:
            return True
    return False


def main() -> int:
    args = parse_args()
    if args.target == "tdeckplus" and args.file == str(DEFAULT_PACKAGED_BIN):
        args.file = str(DEFAULT_TDECKPLUS_BIN)
    if args.target == "tdeckplus" and args.remote_name == DEFAULT_REMOTE_NAME:
        args.remote_name = "PocketSSH-2.0.bin"

    path = pathlib.Path(args.file)
    if not path.exists() and path.resolve() == DEFAULT_PACKAGED_BIN.resolve() and DEFAULT_BUILD_BIN.exists():
        print(f"Packaged binary not found at {path}; falling back to {DEFAULT_BUILD_BIN}")
        path = DEFAULT_BUILD_BIN
    if not path.exists():
        print(f"File not found: {path}", file=sys.stderr)
        return 2

    data = path.read_bytes()
    total = len(data)
    crc32 = zlib.crc32(data) & 0xFFFFFFFF
    chunk = max(1, args.chunk_bytes)

    print(f"Sending {total} bytes from {path}")
    print(f"CRC32: {crc32:08x}")
    print(f"Remote filename: {args.remote_name}")

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=5) as ser:
        # Give the USB CDC/JTAG bridge a brief settle.
        time.sleep(0.2)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        if not args.no_trigger:
            trigger = args.trigger_command.strip() or f"__pocketctl serialrx {args.remote_name}"
            print(f"Triggering receiver: {trigger}")
            ser.write((trigger + "\n").encode("ascii"))
            ser.flush()
            start_offset = wait_for_ready(ser, args.trigger_timeout)
            if start_offset is None:
                print("Timed out waiting for serialrx readiness marker.", file=sys.stderr)
                return 3
        else:
            start_offset = 0
            print("No trigger mode: assuming serialrx is already active at offset 0.")

        if start_offset < 0 or start_offset > total:
            print(f"Device reported invalid resume offset: {start_offset}", file=sys.stderr)
            return 3

        # The USB-JTAG CDC endpoint can deliver its ready log before the RX
        # path is fully switched from serial-control parsing to serialrx.
        # A small settle avoids losing BEGIN and seeing DATA as the header.
        time.sleep(1.0)

        if args.reset_partial:
            header = f"BEGIN {total} {crc32:08x} {start_offset}\n".encode("ascii")
            ser.write(header)
            ser.write(b"ABORT\n")
            ser.flush()
            print(f"Discarded device partial at offset {start_offset}.")
            return 0

        header = f"BEGIN {total} {crc32:08x} {start_offset}\n".encode("ascii")
        # The first line can race the USB-JTAG control-to-receiver handoff.
        # BEGIN is idempotent before DATA; repeat it so at least one copy is
        # consumed by serialrx. Later copies are harmlessly ignored.
        for _ in range(3):
            ser.write(header)
            ser.flush()
            time.sleep(0.04)

        end_offset = total if args.max_bytes <= 0 else min(total, start_offset + args.max_bytes)
        sent = start_offset
        for offset in range(start_offset, end_offset, chunk):
            block = data[offset : offset + chunk]
            line = b"DATA " + block.hex().encode("ascii") + b"\n"
            ser.write(line)
            sent += len(block)

            # Pace slightly to avoid overwhelming the receiver and VFS.
            if (offset // chunk) % 32 == 0:
                ser.flush()
                time.sleep(0.003)

            if total > 0:
                pct = int((sent * 100) / total)
                if pct % 10 == 0 and (sent == len(block) or sent == total):
                    print(f"  {pct}% ({sent}/{total})")

        if end_offset < total:
            ser.write(b"PAUSE\n")
            ser.flush()
            print(f"Segment sent. Waiting for persisted offset {end_offset}...")
            if not wait_for_pause(ser, args.tail_seconds, end_offset, total):
                print("Timed out waiting for serialrx pause evidence.", file=sys.stderr)
                return 4
            print(f"Device persisted resumable segment: {end_offset}/{total} bytes.")
            return 0

        ser.write(b"END\n")
        ser.flush()
        print("Transfer sent. Waiting for device CRC confirmation...")
        if not wait_for_completion(ser, args.tail_seconds, total, crc32):
            print("Timed out waiting for matching serialrx completion evidence.", file=sys.stderr)
            return 4

        print("Device confirmed SD copy size and CRC.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
