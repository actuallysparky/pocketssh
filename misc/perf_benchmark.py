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


FAULT_PATTERNS = {
    "device_reset": re.compile(r"ESP-ROM:", re.I),
    "usb_reset": re.compile(r"USB_UART_CHIP_RESET", re.I),
    "watchdog": re.compile(r"Task watchdog got triggered", re.I),
    "panic": re.compile(r"Guru Meditation Error|panic'ed|abort\(\) was called", re.I),
    "brownout": re.compile(r"Brownout detector was triggered", re.I),
    "ssh_disconnect": re.compile(r"ssh rx: task ended", re.I),
    "ssh_read_error": re.compile(r"Read error: -?\d+", re.I),
    "ssh_eof": re.compile(r"Channel EOF", re.I),
}


def record_faults(line: str, state: dict[str, object]) -> bool:
    if not state.get("fault_tracking", False):
        return False
    counts = state.setdefault("fault_counts", {})
    found = False
    for name, pattern in FAULT_PATTERNS.items():
        if pattern.search(line):
            counts[name] = counts.get(name, 0) + 1
            found = True
    return found


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
            if record_faults(line, state):
                # A fault log can interrupt a metric line on the serial wire.
                state["pending_metrics"] = None
                continue
            sample = consume_metric_line(line, state)
            if sample is not None:
                state["last_sample"] = sample
                samples.append(sample)
    return samples


# A byte threshold alone can stop mid-stream: PTYs may expand LF to CRLF,
# and libssh2 can still have substantial buffered payload at that point.
DRAIN_IDLE_MS = 1000


def trial_is_drained(sample: dict[str, int], target_bytes: int) -> bool:
    """Confirm the measured receive stream has reached its threshold and drained.

    This proves local queue/receive quiescence, not the remote process exit code.
    Legacy telemetry remains parseable but cannot establish queue drainage.
    """
    return (
        sample.get("rx_bytes", 0) >= target_bytes
        and sample.get("transport_window_samples", 0) > 0
        and sample.get("transport_queued_last") == 0
        and sample.get("transport_rx_idle_ms", 0) >= DRAIN_IDLE_MS
    )


def run_trial(port: serial.Serial, raw_log, state: dict[str, object], workload: str,
              payload_bytes: int, timeout_s: float, repaint_mode: str) -> tuple[dict[str, int], bool]:
    state["fault_tracking"] = True
    send_control(port, f"perf repaint {repaint_mode}")
    read_available(port, raw_log, 0.25, state)
    send_control(port, "perf reset")
    read_available(port, raw_log, 0.25, state)
    command, target_bytes = workload_command(workload, payload_bytes)
    send_control(port, f"cmd {command}")

    started = time.monotonic()
    deadline = started + timeout_s
    threshold_elapsed_ms: int | None = None
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
            if state.get("fault_counts"):
                break
            if latest is not None and latest.get("rx_bytes", 0) >= target_bytes:
                if threshold_elapsed_ms is None:
                    threshold_elapsed_ms = int((time.monotonic() - started) * 1000)
                if trial_is_drained(latest, target_bytes):
                    latest["target_bytes"] = target_bytes
                    latest["timed_out"] = 0
                    latest["faulted"] = 0
                    latest["threshold_reached"] = 1
                    latest["threshold_elapsed_ms"] = threshold_elapsed_ms
                    latest["drain_confirmed"] = 1
                    latest["host_elapsed_ms"] = int((time.monotonic() - started) * 1000)
                    return latest, True
        # A streaming stall is benchmark evidence, not a reason to discard the
        # only useful final telemetry sample. Persist it in the summary and stop
        # this run: a wedged remote producer cannot make a later trial comparable.
        timeout_sample = dict(latest or {})
        timeout_sample["target_bytes"] = target_bytes
        timeout_sample["timed_out"] = int(not state.get("fault_counts"))
        timeout_sample["faulted"] = int(bool(state.get("fault_counts")))
        timeout_sample["threshold_reached"] = int(threshold_elapsed_ms is not None)
        if threshold_elapsed_ms is not None:
            timeout_sample["threshold_elapsed_ms"] = threshold_elapsed_ms
        timeout_sample["drain_confirmed"] = 0
        timeout_sample["host_elapsed_ms"] = int((time.monotonic() - started) * 1000)
        return timeout_sample, False
    except KeyboardInterrupt:
        partial = dict(latest or {})
        partial.update(target_bytes=target_bytes, timed_out=0, interrupted=1,
                       drain_confirmed=0, host_elapsed_ms=int((time.monotonic() - started) * 1000))
        return partial, False
    finally:
        # Always restore ordinary rendering. Firmware queues the final repaint
        # on its receive task, avoiding a serial-control/read race in TerminalCore.
        send_control(port, "perf repaint normal")
        read_available(port, raw_log, 0.5, state)


def run_idle_probe(port: serial.Serial, raw_log, state: dict[str, object],
                   seconds: float, repaint_mode: str) -> dict[str, object]:
    """Observe an idle session, then exercise only the fixed screen-fill workload."""
    deadline = time.monotonic() + seconds
    first: dict[str, int] | None = None
    latest: dict[str, int] | None = None
    count = 0
    while time.monotonic() < deadline:
        send_control(port, "perf snapshot")
        for sample in read_available(port, raw_log, min(1.0, max(0.0, deadline - time.monotonic())), state):
            first = first or sample
            latest = sample
            count += 1
        if state.get("fault_counts"):
            return {"requested_idle_seconds": seconds, "samples": count,
                    "first": first, "last": latest, "completed": False}
    response, completed = run_trial(port, raw_log, state, "screen-fill", 512, 30.0, repaint_mode)
    return {"requested_idle_seconds": seconds, "samples": count,
            "first": first, "last": latest,
            "screen_fill": response, "completed": completed and count >= 2}


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
    parser.add_argument("--idle-probe-seconds", type=float, default=0.0,
                        help="After completed trials, observe idle snapshots then run fixed screen-fill")
    parser.add_argument("--pre-wait-seconds", type=float, default=4.0)
    parser.add_argument("--connect-command", default="", help="Optional saved-alias command; not persisted")
    parser.add_argument("--connect-wait-seconds", type=float, default=15.0)
    parser.add_argument("--output-dir", type=Path, default=None)
    args = parser.parse_args()
    if not 0.0 <= args.idle_probe_seconds <= 300.0:
        parser.error("--idle-probe-seconds must be between 0 and 300")
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
    interrupted = False
    idle_probe: dict[str, object] | None = None
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
                    f"timed_out={result.get('timed_out', 0)} "
                    f"faulted={int(bool(serial_state.get('fault_counts')))}",
                    flush=True,
                )
                if not completed or serial_state.get("fault_counts"):
                    timed_out = True
                    break
            if not timed_out and args.idle_probe_seconds > 0:
                idle_probe = run_idle_probe(port, raw_log, serial_state, args.idle_probe_seconds, args.repaint_mode)
                timed_out = not idle_probe["completed"]
    except KeyboardInterrupt:
        interrupted = True
        idle_probe = {"completed": False, "interrupted": True,
                      "last": serial_state.get("last_sample")}
    finally:
        send_control(port, "perf repaint normal")
        port.close()

    key_metrics = (
        "host_elapsed_ms", "threshold_elapsed_ms", "rx_bps", "rx_to_invalidate_p95_ms", "feed_us_max", "draw_us_total", "draw_us_max",
        "lock_timeout", "lock_wait_us_max", "heap_free", "heap_largest", "yields",
        "core_printable_bytes", "core_control_bytes", "core_utf8_bytes", "core_utf8_codepoints",
        "core_cell_writes", "core_scroll_up_ops", "core_scroll_down_ops",
        "core_scrollback_row_copies", "core_dirty_row_marks",
        "transport_rx_idle_ms", "transport_socket_readable_polls", "transport_socket_idle_polls",
        "transport_channel_eagain", "transport_active_flush_attempts", "transport_deferred_flushes",
        "transport_display_update_us_total", "transport_display_update_us_max",
    )
    key_metrics += tuple(f"transport_{key}" for key in TRANSPORT_V4_FIELDS)
    outcome = ("interrupted" if interrupted or any(trial.get("interrupted") for trial in trials)
               else "fault" if serial_state.get("fault_counts")
               else "timeout" if timed_out else "completed")
    summary = {
        "schema": 6,
        "acceptance_rule": "drained-and-no-faults-v1",
        "fault_counts": serial_state.get("fault_counts", {}),
        "completion_rule": "rx-threshold-plus-empty-queue-and-idle-v1",
        "drain_idle_ms": DRAIN_IDLE_MS,
        "transport_versions": sorted({trial.get("transport_transport_version", 3) for trial in trials}),
        "target": "tpager",
        "workload": args.workload,
        "repaint_mode": args.repaint_mode,
        "payload_bytes": workload_command(args.workload, args.payload_bytes)[1],
        "outcome": outcome,
        "completed_trials": sum(1 for trial in trials if trial.get("drain_confirmed", 0) == 1),
        "trials": trials,
        "idle_probe": idle_probe,
        "median": {key: median([trial[key] for trial in trials]) for key in key_metrics
                   if trials and all(key in trial for trial in trials)},
    }
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"saved raw log: {raw_path}")
    print(f"saved summary: {summary_path}")
    return {"completed": 0, "timeout": 2, "fault": 3, "interrupted": 130}[outcome]


if __name__ == "__main__":
    raise SystemExit(main())
