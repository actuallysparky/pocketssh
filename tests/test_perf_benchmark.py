#!/usr/bin/env python3
"""Synthetic tests for content-free T-Pager benchmark telemetry parsing."""

from __future__ import annotations

import sys
import json
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "misc"))
import perf_benchmark  # noqa: E402


class PerfBenchmarkParserTests(unittest.TestCase):
    def setUp(self) -> None:
        self.state: dict[str, object] = {"tail": "", "pending_metrics": None}

    def test_pairs_metric_group_across_interleaved_logs(self) -> None:
        self.assertIsNone(perf_benchmark.consume_metric_line("I tag: unrelated", self.state))
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "I tag: POCKETCTL perf rx_bytes=10 rx_bps=20", self.state))
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "W tag: unrelated", self.state))
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "I tag: POCKETCTL core cell_writes=8 scroll_up_ops=2", self.state))
        sample = perf_benchmark.consume_metric_line(
            "I tag: POCKETCTL transport rx_idle_ms=5 socket_idle_polls=7", self.state)
        self.assertEqual(sample, {
            "rx_bytes": 10,
            "rx_bps": 20,
            "core_cell_writes": 8,
            "core_scroll_up_ops": 2,
            "transport_rx_idle_ms": 5,
            "transport_socket_idle_polls": 7,
        })

    def test_discards_incomplete_group_when_new_perf_arrives(self) -> None:
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "POCKETCTL perf rx_bytes=1", self.state))
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "POCKETCTL core cell_writes=1", self.state))
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "POCKETCTL perf rx_bytes=2", self.state))
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "POCKETCTL transport rx_idle_ms=1", self.state))
        self.assertIsNone(perf_benchmark.consume_metric_line(
            "POCKETCTL core cell_writes=2", self.state))
        sample = perf_benchmark.consume_metric_line(
            "POCKETCTL transport rx_idle_ms=2", self.state)
        self.assertEqual(sample, {
            "rx_bytes": 2,
            "core_cell_writes": 2,
            "transport_rx_idle_ms": 2,
        })

    def v4_line(self, **overrides: int) -> str:
        fields = {"transport_version": 4, **dict.fromkeys(perf_benchmark.TRANSPORT_V4_FIELDS, 0)}
        fields.update(overrides)
        fields["transport_complete"] = 4
        return "POCKETCTL transport " + " ".join(f"{k}={v}" for k, v in fields.items())

    def start_group(self) -> None:
        perf_benchmark.consume_metric_line("POCKETCTL perf rx_bytes=1023", self.state)
        perf_benchmark.consume_metric_line("POCKETCTL core cell_writes=500", self.state)

    def test_v4_separates_synthetic_idle_from_real_eagain(self) -> None:
        self.start_group()
        sample = perf_benchmark.consume_metric_line(self.v4_line(
            select_timeout=8000, read_skipped_idle=8000, read_eagain_ready=2,
            read_positive=1, read_bytes=1023, eagain_block_out=2,
            window_samples=360, window_last=1000, window_initial=2097152,
            queued_last=31000, queued_max=32000, idle_queued_samples=350,
            read_zero=1, errors_socket_recv=1), self.state)
        self.assertIsNotNone(sample)
        self.assertEqual(sample["transport_read_eagain_idle"], 0)
        self.assertEqual(sample["transport_read_eagain_ready"], 2)
        self.assertEqual(sample["transport_read_skipped_idle"], 8000)
        self.assertEqual(sample["transport_queued_last"], 31000)
        self.assertEqual(sample["transport_transport_version"], 4)
        for key in perf_benchmark.TRANSPORT_V4_FIELDS:
            self.assertIn(f"transport_{key}", sample)

    def test_v4_rejects_truncated_or_unknown_extension(self) -> None:
        line = self.v4_line()
        for invalid in ("POCKETCTL transport transport_version=4 rx_idle_ms=5",
                        line.rsplit(" ", 1)[0],
                        line.replace("read_calls=0 ", ""),
                        line.replace("transport_version=4", "transport_version=5")):
            with self.subTest(invalid=invalid):
                self.start_group()
                self.assertIsNone(perf_benchmark.consume_metric_line(invalid, self.state))
                self.assertIsNone(self.state["pending_metrics"])

    def test_new_group_cannot_inherit_v4_extension(self) -> None:
        self.start_group()
        self.assertIsNotNone(perf_benchmark.consume_metric_line(self.v4_line(), self.state))
        self.start_group()
        legacy = perf_benchmark.consume_metric_line("POCKETCTL transport channel_eagain=7", self.state)
        self.assertNotIn("transport_read_eagain_ready", legacy)
        self.assertNotIn("transport_transport_version", legacy)

    def test_completion_requires_quiet_empty_queue_after_threshold(self) -> None:
        sample = {"rx_bytes": 40000, "transport_window_samples": 12,
                  "transport_queued_last": 15000, "transport_rx_idle_ms": 56}
        self.assertFalse(perf_benchmark.trial_is_drained(sample, 32768))
        sample["transport_rx_idle_ms"] = 2000
        self.assertFalse(perf_benchmark.trial_is_drained(sample, 32768))
        sample["transport_queued_last"] = 0
        sample["transport_rx_idle_ms"] = 999
        self.assertFalse(perf_benchmark.trial_is_drained(sample, 32768))
        sample["transport_rx_idle_ms"] = 1000
        self.assertTrue(perf_benchmark.trial_is_drained(sample, 32768))
        sample["rx_bytes"] = 24719
        self.assertFalse(perf_benchmark.trial_is_drained(sample, 32768))

    def test_legacy_or_reset_telemetry_cannot_prove_drainage(self) -> None:
        self.assertFalse(perf_benchmark.trial_is_drained(
            {"rx_bytes": 50000, "transport_rx_idle_ms": 2000}, 32768))
        self.assertFalse(perf_benchmark.trial_is_drained(
            {"rx_bytes": 50000, "transport_queued_last": 0,
             "transport_window_samples": 0, "transport_rx_idle_ms": 2000}, 32768))

    def test_idle_probe_requires_telemetry_and_fixed_workload_response(self) -> None:
        for idle_samples, response_ok, expected in (([{"rx_bytes": 100}, {"rx_bytes": 100}], True, True),
                                                     ([], True, False),
                                                     ([{"rx_bytes": 100}, {"rx_bytes": 100}], False, False)):
            with self.subTest(samples=idle_samples, response=response_ok):
                with patch.object(perf_benchmark.time, "monotonic", side_effect=[0, 0, 0, 2]), \
                     patch.object(perf_benchmark, "send_control"), \
                     patch.object(perf_benchmark, "read_available", return_value=idle_samples), \
                     patch.object(perf_benchmark, "run_trial", return_value=({"rx_bytes": 512}, response_ok)) as trial:
                    result = perf_benchmark.run_idle_probe(None, None, {}, 1, "normal")
                    self.assertEqual(result["completed"], expected)
                    trial.assert_called_once_with(None, None, {}, "screen-fill", 512, 30.0, "normal")

    def test_fault_tracking_ignores_boot_but_rejects_interleaved_watchdog(self) -> None:
        line = "POCKETCTL perf rx_bytes=9000 E (5) task_wdt: Task watchdog got triggered"
        self.assertFalse(perf_benchmark.record_faults(line, {}))
        state = {"fault_tracking": True}
        self.assertTrue(perf_benchmark.record_faults(line, state))
        self.assertEqual(state["fault_counts"], {"watchdog": 1})
        self.assertFalse(perf_benchmark.record_faults("POCKETCTL perf rx_bytes=9000", state))

    def test_trial_preserves_partial_evidence_and_stops_on_fault(self) -> None:
        state = {}
        calls = 0
        def read(*args):
            nonlocal calls
            calls += 1
            if calls == 3:
                state["fault_counts"] = {"watchdog": 1}
                return [{"rx_bytes": 12345}]
            return []
        with patch.object(perf_benchmark.time, "monotonic", return_value=0), \
             patch.object(perf_benchmark, "send_control") as send, \
             patch.object(perf_benchmark, "read_available", side_effect=read):
            sample, completed = perf_benchmark.run_trial(None, None, state, "ascii-scroll", 32768, 90, "normal")
        self.assertFalse(completed)
        self.assertEqual(sample["rx_bytes"], 12345)
        self.assertEqual(sample["faulted"], 1)
        self.assertEqual(sample["timed_out"], 0)
        self.assertEqual(sample["drain_confirmed"], 0)
        self.assertEqual(send.call_args_list[-1].args[1], "perf repaint normal")

    def test_interrupted_trial_retains_result_and_restores_repaint(self) -> None:
        with patch.object(perf_benchmark.time, "monotonic", return_value=0), \
             patch.object(perf_benchmark, "send_control") as send, \
             patch.object(perf_benchmark, "read_available", side_effect=[[], [], KeyboardInterrupt(), []]):
            sample, completed = perf_benchmark.run_trial(None, None, {}, "ascii-scroll", 32768, 90, "normal")
        self.assertFalse(completed)
        self.assertEqual(sample["interrupted"], 1)
        self.assertEqual(sample["drain_confirmed"], 0)
        self.assertEqual(send.call_args_list[-1].args[1], "perf repaint normal")

    def test_idle_fault_does_not_start_another_workload(self) -> None:
        state = {"fault_counts": {"watchdog": 1}}
        with patch.object(perf_benchmark.time, "monotonic", side_effect=[0, 0, 0]), \
             patch.object(perf_benchmark, "send_control"), \
             patch.object(perf_benchmark, "read_available", return_value=[]), \
             patch.object(perf_benchmark, "run_trial") as trial:
            result = perf_benchmark.run_idle_probe(None, None, state, 45, "normal")
        self.assertFalse(result["completed"])
        trial.assert_not_called()

    def test_fault_during_restore_prevents_next_trial(self) -> None:
        def completed_then_fault(port, log, state, *args):
            state["fault_counts"] = {"watchdog": 1}
            return {"rx_bytes": 50000, "drain_confirmed": 1, "timed_out": 0}, True
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "result"
            argv = ["perf_benchmark.py", "--port", "test", "--trials", "3", "--output-dir", str(output)]
            with patch.object(sys, "argv", argv), \
                 patch.object(perf_benchmark.serial, "Serial"), \
                 patch.object(perf_benchmark, "read_available", return_value=[]), \
                 patch.object(perf_benchmark, "send_control"), \
                 patch.object(perf_benchmark, "run_trial", side_effect=completed_then_fault) as trial, \
                 patch("builtins.print"):
                status = perf_benchmark.main()
            self.assertEqual(status, 3)
            trial.assert_called_once()
            self.assertEqual(json.loads((output / "summary.json").read_text())["outcome"], "fault")

    def test_workloads_remain_fixed(self) -> None:
        command, target = perf_benchmark.workload_command("screen-fill", 999)
        self.assertEqual(target, 512)
        self.assertIn("head -c 512", command)
        command, target = perf_benchmark.workload_command("ascii-scroll", 32768)
        self.assertEqual((command, target), ("yes | head -c 32768", 32768))


if __name__ == "__main__":
    unittest.main()
