#!/usr/bin/env python3
"""Benchmark T-Pager SSH terminal streaming through the USB control channel.

The device must already be running a build with ``__pocketctl perf`` support.
The optional connect command is intended for a saved alias after USB/JTAG open
resets the Pager; it is never written to the result files. The benchmark
command and telemetry are content-free, but the raw serial log is retained in
the ignored output directory for diagnosis.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required; use the ESP-IDF Python environment.") from exc


PERF_RE = re.compile(r"POCKETCTL perf (?P<fields>(?:[a-z0-9_]+=[0-9]+ ?)+)")
CORE_RE = re.compile(r"POCKETCTL core (?P<fields>(?:[a-z0-9_]+=[0-9]+ ?)+)")
TRANSPORT_RE = re.compile(r"POCKETCTL transport (?P<fields>(?:[a-z0-9_]+=[0-9]+ ?)+)")

# Version 4 extensions are all-or-nothing; legacy schema-3 groups remain readable.
TRANSPORT_V4_FIELDS = (
    'select_ready',
    'select_timeout',
    'select_error',
    'select_no_fd',
    'select_skipped',
    'read_calls',
    'read_positive',
    'read_bytes',
    'read_eagain_ready',
    'read_eagain_idle',
    'read_skipped_idle',
    'read_zero',
    'errors_socket_recv',
    'errors_socket_send',
    'errors_socket_disconnect',
    'errors_channel_closed',
    'errors_other',
    'eagain_block_none',
    'eagain_block_in',
    'eagain_block_out',
    'eagain_block_both',
    'window_samples',
    'window_last',
    'window_initial',
    'queued_last',
    'queued_max',
    'idle_queued_samples',
)


# These are deliberately fixed POSIX-shell workloads. The host never accepts
# an arbitrary remote command, and the payload itself is uninteresting test
# text rather than user/session content.
WORKLOADS: dict[str, tuple[int | None, str | None]] = {
    "screen-fill": (512, r"head -c 512 /dev/zero | tr '\\000' x"),
    "ascii-scroll": (None, None),
    "ansi-utf8-scroll": (
        53_248,
        r'''i=0; while [ "$i" -lt 4096 ]; do printf '\033[31m\342\234\223\033[0m\n'; i=$((i + 1)); done''',
    ),
}


def parse_metric_line(pattern: re.Pattern[str], text: str) -> dict[str, int] | None:
    match = pattern.search(text)
    if match is None:
        return None
    fields: dict[str, int] = {}
    for field in match.group("fields").split():
        key, value = field.split("=", 1)
        fields[key] = int(value)
    return fields


def workload_command(workload: str, payload_bytes: int) -> tuple[str, int]:
    fixed_payload, command = WORKLOADS[workload]
    if fixed_payload is not None and command is not None:
        return command, fixed_payload
    return f"yes | head -c {payload_bytes}", payload_bytes


def send_control(port: serial.Serial, payload: str) -> None:
    port.write(f"__pocketctl {payload}\n".encode("utf-8"))
    port.flush()


def consume_metric_line(line: str, state: dict[str, object]) -> dict[str, int] | None:
    perf = parse_metric_line(PERF_RE, line)
    if perf is not None:
        # A new perf line supersedes a malformed or incomplete prior group.
        state["pending_metrics"] = {"perf": perf}
        return None
    pending = state.get("pending_metrics")
    if not isinstance(pending, dict):
        return None
    core = parse_metric_line(CORE_RE, line)
    if core is not None:
        pending["core"] = core
        return None
    transport = parse_metric_line(TRANSPORT_RE, line)
    if transport is None:
        return None
    version = transport.get("transport_version")
    extension_present = any(key in transport for key in TRANSPORT_V4_FIELDS)
    if (version is not None or extension_present) and (
        version != 4 or transport.get("transport_complete") != 4 or not all(key in transport for key in TRANSPORT_V4_FIELDS)
    ):
        state["pending_metrics"] = None
        return None
    pending["transport"] = transport
    if not all(isinstance(pending.get(name), dict) for name in ("perf", "core", "transport")):
        return None
    sample = {
        **pending["perf"],
        **{f"core_{key}": value for key, value in pending["core"].items()},
        **{f"transport_{key}": value for key, value in pending["transport"].items()},
    }
    state["pending_metrics"] = None
    return sample


def read_available(port: serial.Serial, raw_log, duration_s: float, state: dict[str, object]) -> list[dict[str, int]]:
    deadline = time.monotonic() + duration_s
    samples: list[dict[str, int]] = []
    while time.monotonic() < deadline:
        data = port.read(4096)
        if not data:
            continue
        raw_log.write(data)
        raw_log.flush()
        tail = str(state["tail"]) + data.decode("utf-8", errors="ignore")
        lines = tail.splitlines(keepends=True)
        state["tail"] = ""
        if lines and not lines[-1].endswith(("\n", "\r")):
            state["tail"] = lines.pop()
        for line in lines:
            sample = consume_metric_line(line, state)
            if sample is not None:
                samples.append(sample)
    return samples


def run_trial(port: serial.Serial, raw_log, state: dict[str, object], workload: str,
              payload_bytes: int, timeout_s: float, repaint_mode: str) -> tuple[dict[str, int], bool]:
    send_control(port, f"perf repaint {repaint_mode}")
    read_available(port, raw_log, 0.25, state)
    send_control(port, "perf reset")
    read_available(port, raw_log, 0.25, state)
    command, target_bytes = workload_command(workload, payload_bytes)
    send_control(port, f"cmd {command}")

    deadline = time.monotonic() + timeout_s
    last_snapshot = 0.0
    latest: dict[str, int] | None = None
    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now - last_snapshot >= 1.0:
                send_control(port, "perf snapshot")
                last_snapshot = now
            for sample in read_available(port, raw_log, 0.15, state):
                latest = sample
            if latest is not None and latest.get("rx_bytes", 0) >= target_bytes:
                send_control(port, "perf snapshot")
                samples = read_available(port, raw_log, 0.5, state)
                if samples:
                    latest = samples[-1]
                latest["target_bytes"] = target_bytes
                latest["timed_out"] = 0
                return latest, True
        # A streaming stall is benchmark evidence, not a reason to discard the
        # only useful final telemetry sample. Persist it in the summary and stop
        # this run: a wedged remote producer cannot make a later trial comparable.
        timeout_sample = dict(latest or {})
        timeout_sample["target_bytes"] = target_bytes
        timeout_sample["timed_out"] = 1
        return timeout_sample, False
    finally:
        # Always restore ordinary rendering. Firmware queues the final repaint
        # on its receive task, avoiding a serial-control/read race in TerminalCore.
        send_control(port, "perf repaint normal")
        read_available(port, raw_log, 0.5, state)


def median(values: list[int]) -> int:
    return int(statistics.median(values)) if values else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="T-Pager USB Serial/JTAG port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--payload-bytes", type=int, default=524288)
    parser.add_argument("--workload", choices=sorted(WORKLOADS), default="ascii-scroll")
    parser.add_argument("--repaint-mode", choices=("normal", "deferred"), default="normal")
    parser.add_argument("--timeout-seconds", type=float, default=300.0)
    parser.add_argument("--pre-wait-seconds", type=float, default=4.0)
    parser.add_argument("--connect-command", default="", help="Optional saved-alias command; not persisted")
    parser.add_argument("--connect-wait-seconds", type=float, default=15.0)
    parser.add_argument("--output-dir", type=Path, default=None)
    args = parser.parse_args()
    if args.trials < 1 or args.payload_bytes < 1:
        parser.error("--trials and --payload-bytes must be positive")
    if args.connect_command and re.fullmatch(r"connect [A-Za-z0-9_.-]+", args.connect_command) is None:
        parser.error("--connect-command must be 'connect <saved-alias>'; direct credentials are not accepted")

    repo_root = Path(__file__).resolve().parents[1]
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output_dir = args.output_dir or repo_root / "_local" / "benchmarks" / stamp
    output_dir.mkdir(parents=True, exist_ok=False)
    raw_path = output_dir / "serial.log"
    summary_path = output_dir / "summary.json"

    port = serial.Serial()
    port.port = args.port
    port.baudrate = args.baud
    port.timeout = 0.1
    port.dtr = False
    port.rts = False
    port.open()
    trials: list[dict[str, int]] = []
    timed_out = False
    serial_state: dict[str, object] = {"tail": "", "pending_metrics": None}
    try:
        with raw_path.open("wb") as raw_log:
            read_available(port, raw_log, max(0.0, args.pre_wait_seconds), serial_state)
            if args.connect_command:
                send_control(port, f"cmd {args.connect_command}")
                read_available(port, raw_log, max(0.0, args.connect_wait_seconds), serial_state)
            for trial in range(args.trials):
                result, completed = run_trial(port, raw_log, serial_state, args.workload,
                                              args.payload_bytes, args.timeout_seconds, args.repaint_mode)
                result["trial"] = trial + 1
                trials.append(result)
                print(
                    f"trial {trial + 1} ({args.workload}): rx_bps={result.get('rx_bps', 0)} "
                    f"draw_us_max={result.get('draw_us_max', 0)} "
                    f"lock_timeout={result.get('lock_timeout', 0)} "
                    f"timed_out={result.get('timed_out', 0)}",
                    flush=True,
                )
                if not completed:
                    timed_out = True
                    break
    finally:
        send_control(port, "perf repaint normal")
        port.close()

    key_metrics = (
        "rx_bps", "rx_to_invalidate_p95_ms", "feed_us_max", "draw_us_total", "draw_us_max",
        "lock_timeout", "lock_wait_us_max", "heap_free", "heap_largest", "yields",
        "core_printable_bytes", "core_control_bytes", "core_utf8_bytes", "core_utf8_codepoints",
        "core_cell_writes", "core_scroll_up_ops", "core_scroll_down_ops",
        "core_scrollback_row_copies", "core_dirty_row_marks",
        "transport_rx_idle_ms", "transport_socket_readable_polls", "transport_socket_idle_polls",
        "transport_channel_eagain", "transport_active_flush_attempts", "transport_deferred_flushes",
        "transport_display_update_us_total", "transport_display_update_us_max",
    )
    key_metrics += tuple(f"transport_{key}" for key in TRANSPORT_V4_FIELDS)
    summary = {
        "schema": 4,
        "transport_versions": sorted({trial.get("transport_transport_version", 3) for trial in trials}),
        "target": "tpager",
        "workload": args.workload,
        "repaint_mode": args.repaint_mode,
        "payload_bytes": workload_command(args.workload, args.payload_bytes)[1],
        "outcome": "timeout" if timed_out else "completed",
        "completed_trials": sum(1 for trial in trials if trial.get("timed_out", 0) == 0),
        "trials": trials,
        "median": {key: median([trial[key] for trial in trials]) for key in key_metrics
                   if trials and all(key in trial for trial in trials)},
    }
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"saved raw log: {raw_path}")
    print(f"saved summary: {summary_path}")
    return 2 if timed_out else 0


if __name__ == "__main__":
    raise SystemExit(main())
