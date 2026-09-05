# Handoff prompt — continue T-Pager streaming performance investigation

You are continuing evidence-first performance work in
`/Users/sparky/engineering/projects/PocketSSH` on branch `pocketssh-2.0`.
Start by reading this file, `AGENTS.md`, `journal.md`, and
`docs/performance/tpager-scroll-experiment-2026-09-03.md`. Inspect `git
status --short` before editing; current completed work is committed locally as
`60e6545` (instrumentation and optimizations) and `19f425f` (T-Deck delivery
journal). Do not push to a remote.

## Objective

Find and validate the next performance opportunity for rapid T-Pager terminal
scrolling. The local terminal and renderer are now substantially faster, but
the SSH receive stream later stalls. Establish whether the residual cause is
the remote producer/TCP/SSH channel window, the libssh2 receive integration, or
another local receive-loop condition before changing behavior.

Keep this work evidence-first. Do not infer end-to-end throughput from
active-window `rx_bps` when telemetry reports a long trailing `rx_idle_ms`.
Do not change terminal semantics, ANSI/UTF-8 behavior, scrollback capacity,
the row-storage model, LCD/DMA settings, or SSH configuration unless a
separate evidence-backed proposal is accepted.

## Established baseline and gains

All canonical device values below are **single timeout samples**, not medians.
The fixed workload is a 512 KiB `ascii-scroll` stream (`yes | head -c N`) over
the saved, key-backed `prodmini` POSIX-shell alias, normal font mode, same
Wi-Fi, and a 300-second ceiling.

| Comparison | Active RX B/s | Received | Feed total / peak | p95 invalidate | Draw total | Lock timeouts | Result |
| --- | ---: | ---: | --- | ---: | ---: | ---: | --- |
| Initial instrumentation | 3,069 | 164,010 B | not retained / 300 ms | 305 ms | 6.92 s | 11 | timed out |
| Renderer/scheduling A-side | 3,266 | 164,025 B | 47.64 s / 306 ms | 305 ms | 5.55 s | 7 | timed out |
| Row-rotation B-side | 12,906 | 164,727 B | 7.41 s / 52 ms | 240 ms | 2.96 s | 66 | timed out |

The best completed local combination is therefore roughly **4.2x** the
initial active receive rate, 84% less feed time, and 57% less draw time. It is
not a 4.2x end-to-end throughput claim because all canonical trials stopped
near 165 KiB and timed out.

Host eleven-run medians show the terminal-core row-rotation win is real:

| Host workload | Before | After | Change |
| --- | ---: | ---: | ---: |
| ASCII scrolling | 11,483 us/MiB | 8,068 us/MiB | -29.7% |
| ANSI + UTF-8 scrolling | 11,676 us/MiB | 8,369 us/MiB | -28.3% |

## What Round 2 established

The current T-Pager diagnostic build includes the earlier bounded 4 KiB/10 ms
receive fairness yield, 50 ms active repaint cadence, clipped/coalesced grid
renderer, row rotation/reuse, and content-free schema-3 telemetry:

```text
POCKETCTL perf ...
POCKETCTL core ...
POCKETCTL transport ...
```

`__pocketctl perf repaint normal|deferred` is T-Pager serial-lab-only. In
deferred mode SSH reads and `TerminalCore::feed()` continue but receive-loop
flushes are suppressed; the benchmark helper restores normal mode in `finally`
and queues a final repaint.

Matched 32 KiB runs (90-second ceiling) produced:

| Mode | Received | Idle receive | Feed total / max | Draw | Lock timeouts | Socket readable / idle | Channel EAGAIN |
| --- | ---: | ---: | --- | --- | ---: | --- | ---: |
| Normal | 25,973 B | 87.105 s | 1.156 s / 49.642 ms | 0.701 s | 9 | 30 / 8,750 | 8,750 |
| Deferred | 26,537 B | 88.550 s | 1.011 s / 40.971 ms | 0 | 0 | 28 / 8,885 | 8,885 |

Deferred repaint removed renderer and display-lock work yet did not allow the
stream to complete. The current best classification is an SSH/remote
flow-control stall rather than display backpressure. That conclusion is strong
enough to reject a display worker as the next change, but the transport
counters may still conflate `select()` readiness and later channel read state;
the next instrumentation should remove that ambiguity.

## Recommended next experiment sequence

1. **Read the receive loop before changing it.** Identify exactly when
   `select()` is called, when `libssh2_channel_read()` is called, and why the
   current socket-idle and EAGAIN counters rise together. Preserve watchdog
   safety and the existing idle polling contract.

2. **Add only diagnostic counters first.** Keep telemetry content-free and
   extend the `POCKETCTL transport` line with, at minimum:
   - select-ready versus select-timeout/error outcomes;
   - positive channel-read calls and bytes;
   - EAGAIN after a readable socket versus EAGAIN after an idle socket;
   - libssh2 block direction and available channel receive-window size, if
     supported by the bundled libssh2 API;
   - channel EOF/error codes as counts, never remote content.

   Update `misc/perf_benchmark.py` atomically, add synthetic parser tests for
   the new fields, and retain schema compatibility deliberately (either a new
   schema version or explicit optional fields—do not silently mix metrics).

3. **Run the smallest discriminating physical matrix before optimizing.** On a
   fresh, registered T-Pager receipt and only after explicit artifact-bound
   delivery authority, run three trials where possible under the unchanged
   Wi-Fi/font/`prodmini` conditions:
   - normal 32 KiB `ascii-scroll` (90 s), retaining the first partial sample
     on timeout;
   - deferred 32 KiB only if the repaint probe remains useful for comparison;
   - canonical 512 KiB `ascii-scroll` (300 s) only if the shorter condition
     completes, unless the purpose is explicitly a partial-stall diagnostic.

   If remote-side observation is authorized and available, collect only
   content-safe connection/window/producers-state summaries for the fixed
   workload. Do not record credentials or terminal output, and do not extend
   the benchmark helper to accept arbitrary remote commands.

4. **Make one narrow change only after classifying the evidence.**
   - Predominantly `select()` idle with no channel readability: investigate
     remote producer/TCP/SSH-window behavior before terminal code.
   - Readable socket followed by EAGAIN: isolate the libssh2 polling/blocking
     contract, `libssh2_session_block_directions`, and channel-read scheduling.
   - Meaningful terminal feed time while data stays readable: profile
     terminal-core mutation beyond row rotation before proposing a storage
     redesign.
   - Do not return to display-worker work unless a new control trial shows
     repaint causally changes completion.

5. **Acceptance and report-out.** Keep raw serial and JSON ignored under
   `_local/benchmarks/`; publish a concise append-only section in
   `tpager-scroll-experiment-2026-09-03.md` and `journal.md` with initial,
   current, and new-trial values. Distinguish full completed trials from
   partial timeout samples. Record watchdog/panic/brownout/disconnect evidence
   explicitly. Run:

   ```sh
   ./tests/run_terminal_core_tests.sh
   python tests/test_perf_benchmark.py
   python -m py_compile misc/perf_benchmark.py
   ./tests/run_terminal_core_benchmarks.sh
   ./build.sh tpager
   ./build.sh tdeckplus
   ./package.sh tpager
   ./package.sh tdeckplus
   git diff --check
   ```

   Commit only scoped source/tests/docs/journal work using the repository-local
   GitHub no-reply identity. Do not push. Hardware evidence, flashing, and UI
   experience are separate from source/build validation.

## Device and artifact safety

T-Pager is the physical performance target, registered as MAC
`10:20:ba:33:fa:9c` under the resident `projects/pagerdeck/Launcher` lineage.
PocketSSH occupies Launcher Slot A, `pocket` at `0x1A0000`. Use the
`codex_device_ops` identity broker, bind the receipt to the exact artifact, and
write only the receipt-authorized Slot-A app region. Do not infer device
identity from a port name. A new agent must not assume that a prior flash
authorizes a new flash; obtain current user authorization and a fresh receipt.

The latest T-Pager diagnostic package tested in Round 2 was SHA-256
`3ee9656c435c288da6e0e6fb2fed89097101b7a54a4998ab8a506c126f7ab8a9`.
The combined source is committed in `60e6545`; rebuild instead of assuming an
ignored local artifact is still current.

## Evidence locations

- Durable report: `docs/performance/tpager-scroll-experiment-2026-09-03.md`
- Round-2 normal raw/summary: `_local/benchmarks/round2-normal32k-20260903/`
- Round-2 deferred raw/summary:
  `_local/benchmarks/round2-deferred32k-20260903/`
- Canonical row-rotation B-side:
  `_local/benchmarks/post-ascii512k-20260903T190613Z/`
- Pre-rotation A-side:
  `_local/benchmarks/pre-ascii512k-20260903T185015Z/`
- Host microbenchmarks: `_local/benchmarks/host/`
