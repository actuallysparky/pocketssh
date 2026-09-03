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

    def test_workloads_remain_fixed(self) -> None:
        command, target = perf_benchmark.workload_command("screen-fill", 999)
        self.assertEqual(target, 512)
        self.assertIn("head -c 512", command)
        command, target = perf_benchmark.workload_command("ascii-scroll", 32768)
        self.assertEqual((command, target), ("yes | head -c 32768", 32768))


if __name__ == "__main__":
    unittest.main()
