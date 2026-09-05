#!/usr/bin/env python3
"""Synthetic tests for content-free T-Pager benchmark telemetry parsing."""

from __future__ import annotations

import sys
import unittest
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

    def test_workloads_remain_fixed(self) -> None:
        command, target = perf_benchmark.workload_command("screen-fill", 999)
        self.assertEqual(target, 512)
        self.assertIn("head -c 512", command)
        command, target = perf_benchmark.workload_command("ascii-scroll", 32768)
        self.assertEqual((command, target), ("yes | head -c 32768", 32768))


if __name__ == "__main__":
    unittest.main()
