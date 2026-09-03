# T-Pager terminal-core scroll experiment — 2026-09-03

## Result

Replacing visible-row cell-vector copying with row rotation/reuse materially improves terminal-core streaming work. It is not a full acceptance: the canonical 512 KiB workload still timed out after about 165 KiB and display-lock timeouts increased. The next investigation should focus on the remaining transport/receive stall and display-lock contention, not a circular screen buffer or ANSI parser rewrite.

## Method

The fixed T-Pager workload uses the same Wi-Fi, normal font mode, `prodmini` saved key-backed POSIX alias, 300-second timeout, and 512 KiB `ascii-scroll` command. `POCKETCTL perf` snapshots are paired with content-free `POCKETCTL core` operation counts. Initial and current rows below are single timeout samples, not three-trial medians.

| Comparison | Artifact | RX B/s | Feed max | Feed total | p95 RX-to-invalidate | Draw total | Lock timeouts |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Initial instrumentation | `8c120a5` | 3,069 | 300 ms | not retained | 305 ms | 6.92 s | 11 |
| Current A-side | `60496edd` | 3,266 | 306 ms | 47.64 s | 305 ms | 5.55 s | 7 |
| Post B-side rotation | `12b1cdbb` | 12,906 | 52 ms | 7.41 s | 240 ms | 2.96 s | 66 |

The B-side retains the same scroll operation volume as A (54,880 versus 54,645 scroll-up/scrollback-copy operations), so the gain comes from moving row ownership rather than reducing terminal work. It reduced feed total time 84%, feed peak 83%, and draw time 47%. The canonical completion and no-display-lock-regression gates are not met. No raw log contains watchdog, panic, brownout, or SSH-session-disconnect evidence.

## Host A/B microbenchmark

Eleven-run medians in microseconds per MiB; raw values are retained under ignored `_local/benchmarks/host/`.

| Workload | A-side | B-side | Change |
| --- | ---: | ---: | ---: |
| Screen fill | 5,056 | 4,619 | -8.6% |
| ASCII scrolling | 11,483 | 8,068 | -29.7% |
| ANSI + UTF-8 scrolling | 11,676 | 8,369 | -28.3% |
| Scrollback viewport | 20 | 20 | unchanged |

Host ASCII scrolling exceeds the 25% target. Terminal-core host regressions, T-Pager and T-Deck Plus builds, package checks, and whitespace validation passed. The valid ANSI/UTF-8 workload had fixed shell quoting; the earlier 300-byte malformed-command capture is excluded from this comparison.

## Device diagnostic samples

| Workload | A-side RX B/s | B-side RX B/s | Outcome |
| --- | ---: | ---: | --- |
| Screen fill, 512 B | 39,500 median of 3 | 25,392 median of 3 | completed; too short for stable throughput comparison |
| ASCII scroll, 32 KiB | 3,197 | 10,069 | first trial timed out |
| ANSI + UTF-8 scroll, 53,248 B | 9,747 | 16,524 | first trial timed out |
| ASCII scroll, 512 KiB | 3,266 | 12,906 | first trial timed out |

The canonical samples received 164,025 B (A) and 164,727 B (B), so row rotation removes local processing delay but does not remove the later stalled remote stream. Future trials should retain the fixed workload and investigate why receive activity ends near that boundary before altering terminal storage.

## Round 2 — receive-stall and display-lock probe

This lab-only diagnostic artifact adds a third, paired `POCKETCTL transport`
line and a deferred-repaint control. Deferred mode continues SSH receive and
`TerminalCore::feed()` but suppresses receive-loop display flushes; normal mode
is restored in the benchmark helper's `finally` path and requests one final
flush. Snapshot parsing is schema 3 and accepts a sample only when its
`perf`, `core`, and `transport` lines are complete and in order.

The diagnostic artifact was `3ee9656c435c288da6e0e6fb2fed89097101b7a54a4998ab8a506c126f7ab8a9`
(T-Pager, 1,766,000 bytes). A fresh receipt verified MAC
`10:20:ba:33:fa:9c`, the Launcher lineage, and Slot A before it wrote only
`pocket` at `0x1A0000`. Terminal tests, parser tests (including interleaved
and incomplete groups), Python syntax, host microbenchmarks, both target
builds, both packages, and `git diff --check` passed before delivery.

| Comparison | Mode / result | RX bytes | Elapsed / idle receive | Feed total / max | Draw total / max | Locks | Socket / channel evidence | Heap |
| --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| Initial | instrumentation 512 KiB timeout | 164,010 | 300 s / not retained | not retained / 300 ms | 6.92 s / not retained | 11 timeouts | full-grid renderer, no transport counters | not retained |
| Current | row-rotation 512 KiB timeout | 164,727 | 300 s / not retained | 7.41 s / 52 ms | 2.96 s / not retained | 66 timeouts | no transport counters | not retained |
| Post-trial normal | 32 KiB timeout (first of 3) | 25,973 | 90.070 s / 87.105 s | 1.156 s / 49.642 ms | 0.701 s / 8.606 ms | 9 timeouts | 30 readable, 8,750 idle polls; 8,750 EAGAIN | 7,769,839 free; 7,602,176 largest |
| Post-trial deferred | 32 KiB timeout (first of 3) | 26,537 | 90.240 s / 88.550 s | 1.011 s / 40.971 ms | 0 / 0 | 0 timeouts | 28 readable, 8,885 idle polls; 8,885 EAGAIN | 7,774,375 free; 7,602,176 largest |

`rx_bps` is active-window rate only (10,078 normal; 19,215 deferred) and is
not end-to-end throughput: each run spent roughly 88 seconds with no received
byte. Both modes stopped after about 26 KiB, but deferred mode eliminated all
draw work and display-lock timeouts. The plan therefore stops here: deferred
32 KiB did not complete three trials, so no non-comparable deferred 512 KiB
workload was run. The normal 32 KiB result did not justify an additional normal
512 KiB confirmation trial; the current B-side canonical comparator remains
the applicable normal reference.

| Comparison point | Required headline evidence |
| --- | --- |
| Initial | Single 512 KiB timeout: 3,069 active RX B/s, 305 ms p95, 300 ms feed peak, 6.92 s draw total, 3,995,460 examined cells. |
| Current | Single row-rotation 512 KiB timeout: 12,906 active RX B/s, 52 ms feed peak, 240 ms p95, 66 display-lock timeouts. |
| Post-trial | Both 32 KiB modes timed out on their first trial; normal/deferred received 25,973/26,537 B. Deferred had 0 draw work and 0 lock timeouts but 88.550 s receive idle, 8,885 idle polls, and 8,885 channel EAGAIN. |

### Interpretation

This **confirms SSH/remote flow control as the current stall class**, not
display backpressure. With repaint deferred, terminal mutation remained under
1.02 seconds total, the renderer and display lock were entirely inactive, and
the receive task nevertheless spent 88.55 seconds in socket-idle polls with a
matching channel-EAGAIN count. The decision rule for a display worker is not
met. The next experiment should inspect the fixed producer's TCP/SSH window and
libssh2 receive behavior while preserving this workload; in particular, capture
remote-side producer/socket state and distinguish whether a peer-side window or
local libssh2 polling contract prevents further readability. No raw log
contains watchdog, panic, brownout, or SSH-session-disconnect evidence.

Raw serial and schema-3 JSON are retained under ignored
`_local/benchmarks/round2-normal32k-20260903/` and
`_local/benchmarks/round2-deferred32k-20260903/`.
