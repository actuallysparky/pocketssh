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

## 2026-09-04T17:29:00-07:00 — Round 3 diagnostic preparation

Source inspection corrects the strength of the Round-2 interpretation above.
The receive loop performs an immediate, read-only `select()` before each
channel read. An idle socket **skips libssh2 entirely** and assigns a synthetic
`LIBSSH2_ERROR_EAGAIN`, which increments the old `channel_eagain` counter.
Thus the matching 8,750/8,885 idle/EAGAIN counts are not evidence that libssh2
returned EAGAIN. Deferred repaint still rejects display backpressure as the
next optimization target, but remote flow control versus local receive
integration remains unresolved.

The bundled `skuodi/libssh2_esp` 1.1.0 source provides a concrete local
hypothesis: `_libssh2_channel_read()` in `libssh2/src/channel.c` processes
pending transport packets before copying at most the requested 1,023 bytes
into PocketSSH's buffer. Remaining channel data can already be queued when
the next socket readiness check is idle. Requiring another readable TCP
socket could then strand buffered payload. This is source-supported reasoning,
not a new physical finding; no receive scheduling or window adjustment has
been changed.

The content-free transport extension begins with `transport_version=4` and
ends with `transport_complete=4`. The host accepts version 4 only with all
extension fields and its completion marker, emits summary schema 4, and lists
observed transport versions. Legacy unversioned schema-3 groups retain their
old meanings; absent fields are omitted from medians rather than filled with
zero. Existing perf/core/transport grouping and fixed workloads remain.

| Fields | Meaning |
| --- | --- |
| `select_ready`, `select_timeout`, `select_error`, `select_no_fd`, `select_skipped` | Separate select outcomes; no-fd means positive return without the monitored descriptor set. Skipped means no eligible socket/connection. |
| `read_calls`, `read_positive`, `read_bytes` | Actual channel calls, positive returns, and returned payload bytes. |
| `read_eagain_ready`, `read_eagain_idle`, `read_skipped_idle` | Real EAGAIN after readiness; real EAGAIN after idle (always zero under the retained gate); synthetic idle outcomes with no channel call. Old `channel_eagain` still includes synthetic outcomes. |
| `eagain_block_none`, `eagain_block_in`, `eagain_block_out`, `eagain_block_both` | Block-direction categories sampled immediately after actual EAGAIN, before keepalive or other receive-loop work. Not idle-socket state. |
| `read_zero`, `errors_socket_recv`, `errors_socket_send`, `errors_socket_disconnect`, `errors_channel_closed`, `errors_other` | Zero-return/legacy EOF-path count and actual read errors grouped by libssh2 codes -43, -7, -13, -26, and remaining errors. No new EOF probe. |
| `window_samples`, `window_last`, `window_initial`, `queued_last`, `queued_max`, `idle_queued_samples` | Receive-owner samples at most every 250 ms before a read decision; local SSH receive window and queued channel bytes, including extended data. Idle queued samples count observations, not bytes or read calls. Values require a nonzero sample count. |

The bundled `libssh2_channel_window_read_ex()` only reads local window fields
and walks the packet queue; it performs no socket I/O or window adjustment.
`libssh2_session_block_directions()` reads the stored direction mask. No
libssh2 calls were added to the serial snapshot task. Queue sampling adds
bounded-frequency diagnostic work; its runtime overhead remains unmeasured.
Counters are relaxed atomic observations, not a transaction across a running
receive task or a concurrent reset.

| Comparison | Received / active RX rate | Outcome |
| --- | --- | --- |
| Initial canonical 512 KiB | 164,010 B / 3,069 B/s | Single 300-second timeout. |
| Current row-rotation canonical 512 KiB | 164,727 B / 12,906 B/s | Single 300-second timeout; not end-to-end throughput. |
| Round-2 normal / deferred 32 KiB | 25,973 / 26,537 B | First trials timed out at 90 seconds; 87.105 / 88.550 seconds trailing receive idle. |
| Round-3 new device trial | Not collected | Diagnostic delivery awaits fresh artifact-bound user authorization and broker receipt. |

Next run normal 32 KiB ASCII scrolling on `prodmini`, normal font and unchanged
Wi-Fi, with a 90-second ceiling and up to three completed trials; retain the
first timeout and stop. Persistent idle queued samples would prioritize the
local readiness gate; idle with an empty queue would prioritize authorized
remote producer/TCP/window observation. Actual readable-socket EAGAIN and its
block direction would instead prioritize the libssh2 retry contract. Queue
bytes include extended data, so they alone do not prove stream-0 payload is
available. Deferred comparison is optional only if new repaint ambiguity
arises. Proceed to canonical 512 KiB/300 seconds only after the short gate
completes, unless separately designated a partial-stall diagnostic. Preserve
watchdog safety and the 10 ms idle polling contract in any later proposal.

No new physical serial evidence exists: watchdog, panic, brownout, disconnect,
boot, and UI behavior are **not observed for this artifact**. No device or
remote producer was accessed, and no behavior optimization was made.

### 2026-09-04T17:30:29-07:00 — Local validation and delivery candidates

Validation: `./tests/run_terminal_core_tests.sh` passed (including wrapper
contracts); `python tests/test_perf_benchmark.py` passed all six tests;
`python -m py_compile misc/perf_benchmark.py` passed. Python commands used
`/Users/sparky/.espressif/python_env/idf5.5_py3.14_env/bin/python` because plain
`python` is absent from the default shell PATH. The eleven-run host benchmark
passed with medians 5,127 screen-fill, 7,597 ASCII-scroll, 7,794 ANSI/UTF-8-scroll,
and 20 scrollback-viewport microseconds/MiB, saved under
`_local/benchmarks/host/20260905T002633Z/`. These are current host validation
samples, not a new paired optimization experiment or device throughput claim.

`./build.sh tpager`, `./build.sh tdeckplus`, both `./package.sh` targets, package
byte equality with their built app images, and `git diff --check` passed.
The initial sandboxed build failed in ESP-IDF component-manager process
inspection (`psutil`/`sysctl`); the approved build retry outside the sandbox
passed. Builds retain warnings in unmodified dependencies/Kconfig; no compiler
warning was reported against the modified `main/ssh_terminal.cpp`.
Build logs and generated artifacts remain ignored under `_local/`.

- `tpager`: `PocketSSH-TPager.bin`, 1,770,016 bytes, SHA-256 `ad0a1f70eb6bbf2a96fafee1c3ca832d40ecf22bd490210ed2265b1c67ad98e0`.
- `tdeckplus`: `PocketSSH-2.0.bin`, 4,247,360 bytes, SHA-256 `a8b46a8cb89c73f3a38a26f510ee54d9d72312f2de58ed87cc087e101d87dd21`.

The T-Pager package is 64,992 bytes smaller than the historically observed
1,835,008-byte Launcher Slot A. The build wrapper's 81% headroom refers to
its standalone partition layout and is not Launcher Slot-A evidence. A fresh
broker receipt and live slot verification remain mandatory before delivery.

### 2026-09-04T17:39:00-07:00 — Authorized diagnostic delivery and physical result

The user explicitly authorized flashing the connected Pager after the diagnostic
package was ready. Source commit `f1d221a` was committed locally with the
repository's GitHub no-reply identity. The broker initially rejected the new
repository lineage name because its installed policy still maps PocketSSH under
`projects/pagerdeck/PocketSSH`; inspection of that policy confirmed the same
PocketSSH/Launcher Slot-A mapping. Using that policy key with the current-root
artifact produced receipt `5c811c7d-603f-47fb-92c6-b147e30500ea`, verified MAC
`10:20:ba:33:fa:9c`, and freshly read the live `pocket`/OTA-0 partition at
`0x1A0000`, size 1,835,008 bytes. The broker wrote only that region and verified
SHA-256 `ad0a1f70eb6bbf2a96fafee1c3ca832d40ecf22bd490210ed2265b1c67ad98e0`.
Launcher, Slot B, bootloader, partition table, and data were outside the write.
No broker policy or registry was changed.

A 15-second broker monitor was quiet. The subsequent benchmark USB open
captured boot, keyboard initialization PASS, an active SSH receive worker,
and `libssh2 blocking=0`; the fixed saved `prodmini` reconnect received data.
The first sandboxed helper attempt failed before opening USB; the approved
retry ran the normal-repaint 32 KiB ASCII workload with a 90-second ceiling and
three requested trials, stopping and retaining the first timeout as designed.
Wi-Fi/font settings were not changed; this was not a separate visual font/UI
acceptance check. The helper's normal-repaint restoration was acknowledged.

| Comparison | RX bytes / active B/s | Elapsed / trailing idle | Feed total / max | Draw total / max | p95 invalidate / lock timeouts | Outcome |
| --- | --- | --- | --- | --- | --- | --- |
| Initial 512 KiB | 164,010 / 3,069 | 300 s / not retained | not retained / 300 ms | 6.92 s / not retained | 305 ms / 11 | Single timeout |
| Current row-rotation 512 KiB | 164,727 / 12,906 | 300 s / not retained | 7.41 s / 52 ms | 2.96 s / not retained | 240 ms / 66 | Single timeout |
| New diagnostic normal 32 KiB | 24,719 / 9,875 | 90.140 s / 87.311 s | 1.123622 s / 51.057 ms | 0.697587 s / 7.871 ms | 305 ms / 7 | First trial timed out; zero completed trials |

The final version-4 sample reports 26 select-ready polls and 26 actual reads,
all positive, returning exactly 24,719 bytes. It reports 8,763 select timeouts
and 8,763 skipped reads, **zero actual EAGAIN**, zero select errors/skipped
selects/no-fd outcomes, zero channel read errors, and zero zero-byte/EOF-path
returns. All EAGAIN-direction counts are zero because no actual EAGAIN occurred.
Of 358 queue/window samples, 349 observed an idle socket with queued data.
The last and maximum queue value were 24,608 bytes; the last receive window
was 2,072,204 bytes against an initial 2,097,152. Heap free/largest were
7,757,939/7,602,176 bytes. These are one partial timeout sample, not medians;
9,875 B/s is not end-to-end throughput.

This locates a concrete remaining opportunity in the local receive integration:
queued channel data persists while the application declines to call libssh2.
The sampled local SSH receive window is not exhausted. Remote producer state
and TCP windows were not independently observed, and queued bytes include
extended data, so this does not prove all remote output is complete or that
every queued byte is standard output. It does justify testing standard-channel
queue readiness before any remote configuration or terminal-storage change.
Deferred repaint is not needed to resolve this observation, and the failed
short gate does not justify the canonical 512 KiB run yet.

No watchdog, panic, brownout, SSH-disconnect, receive-task-exit, read-error, or
EOF marker occurred during the trial. Raw serial, schema-4 JSON, and a copy of
the exact flashed diagnostic image are ignored under
`_local/benchmarks/round3-normal32k-20260905T0034Z-retry/`. Host/source/build,
flash/hash verification, the partial stream trial, and subjective UI acceptance
remain separate evidence layers.

### Buffered standard-channel candidate — awaiting physical validation

After the diagnostic trial, the single behavior change admits a channel read
when either TCP is readable or `libssh2_poll_channel_read(channel, 0) == 1`.
The bundled function in `libssh2/src/session.c` only examines queued packet
types; it does not poll the socket or receive data. Standard channel data is
required; extended-only queues and negative probe errors do not qualify.
Empty-idle passes still skip `channel_read` and retain the 10 ms delay;
nonblocking mode, the 4 KiB/10 ms fairness yield, rendering, terminal semantics,
UTF-8/ANSI, scrollback, storage, LCD/DMA, and SSH configuration are unchanged.
A zero result from an eligible read follows the existing EOF path, without an
idle EOF probe.

Version-4 field meanings remain unchanged. `read_eagain_idle`, previously zero
because no idle-socket read was attempted, now counts real EAGAIN after a
queue-qualified idle-socket read. Read calls can exceed select-ready polls;
`read_skipped_idle` still counts only synthetic idle results. The diagnostic
artifact and this candidate must be compared by exact artifact hash.

This candidate is not on the Pager yet. Fresh authorization bound to the new
package and a fresh broker receipt are required before another Slot-A flash.
Then rerun the normal 32 KiB gate, inspect queue drainage and idle/watchdog
behavior, and advance to canonical 512 KiB only if the short trials complete.

2026-09-04T17:39:46-07:00 — Candidate validation passed: terminal/wrapper tests, six Python parser tests, Python byte compilation, eleven-run host microbenchmarks, both target builds, both target packages with built-image byte equality, and `git diff --check`. Host medians were 5,110 screen-fill, 7,571 ASCII-scroll, 7,784 ANSI/UTF-8-scroll, and 20 viewport us/MiB; these validate the unchanged core and do not test the embedded receive loop. Evidence is under `_local/benchmarks/host/20260905T003613Z/` and `_local/benchmarks/round3-buffered-*-build.log`. No new receive-loop runtime/watchdog acceptance is claimed.

- Candidate `tpager`: 1,770,160 bytes; SHA-256 `411605e11ed9599c18f776521ebc44a292e66a3a14fc0b39f821908b764a6c88`.
- Candidate `tdeckplus`: 4,247,504 bytes; SHA-256 `fefd045184d863b5618488eb630fb98bcacf90dda771ed88cfd064b8f12a67c0`.

## 2026-09-04T18:14:08-07:00 — Round 4: queue fix works; watchdog gate rejects the build

The user requested continued work through a build suitable for acceptance.
Fresh broker receipt `3872285e-54a6-4220-8797-52cf39c2ac59` verified the registered
Pager and live Slot A, then wrote only candidate SHA-256
`411605e11ed9599c18f776521ebc44a292e66a3a14fc0b39f821908b764a6c88` to `pocket`
at `0x1A0000`. No other partition was written.

The old helper reported three short trials completed, but their final snapshots
still had 11,879–15,335 queued bytes. Those byte-threshold-only runs under
`_local/benchmarks/round4-buffered-normal32k/` are not full-drain acceptance:
PTY expansion allows RX to cross the producer-byte target early, and subsequent
trials can overlap leftover output. The fixed remote workloads remain unchanged.
The corrected helper requires the RX threshold, a sampled empty libssh2 queue,
and at least 1,000 ms trailing receive idle. It records host elapsed time
including settlement. Schema 5 introduced this rule; legacy telemetry remains
parseable but missing queue measurements cannot prove drainage. Local quiescence
does not independently report the remote producer exit status.

With that stronger criterion, normal 32 KiB trials all drained: 49,327 received
bytes each; host elapsed 5,368/5,358/5,372 ms; last queue zero and receive idle
1,225/1,282/1,085 ms. Read calls 51/50/51 exceeded select-ready counts 28/25/28,
showing reads proceeded from buffered channel data without new TCP readability.
Raw evidence: `_local/benchmarks/round4-drained-normal32k/`.

The canonical 512 KiB run then drained 786,608 bytes per trial, demonstrating
the previous receive stall is removed, but **the build is rejected**: nine
IDLE0 watchdog triggers occurred during later sustained receive work. No panic
or brownout was observed. The idle probe was interrupted instead of continuing
stress. Its interrupted schema-5 helper did not write its final summary, so
`_local/benchmarks/round4-drained-normal512k/recovered-evidence.json` explicitly
labels recovered raw-serial samples and unavailable host timings; the raw log,
exact failing binary/ELF and decoded backtraces are retained alongside it.
Do not turn these drained-byte results into a fault-free performance median.

Backtraces resolve to `TerminalCore::scroll_up`/feed and the receive-owner queue
sampler, not a stuck channel read. The built target has
`CONFIG_FREERTOS_HZ=100`: `pdMS_TO_TICKS(1)` is zero, so the existing fairness
call only yields to ready peers and does not block long enough to let IDLE0 run.
Previously, socket-idle sleeps masked this during stalled output. The next
narrow fix is `vTaskDelay(1)` at the same 4 KiB/10 ms fairness boundary: one real
RTOS tick. No watchdog timeout, configuration, terminal semantics, storage,
LCD/DMA setting, or SSH setting is relaxed.

The host helper is now schema 6 and additionally rejects watchdog, panic,
brownout, SSH EOF/read-error, and receive-task-exit markers, stopping further
workloads and retaining partial counters. Interruptions also retain available
summary evidence. The optional idle probe samples the same session before a
fixed screen-fill response check; it does not accept arbitrary remote commands.
Synthetic tests cover early-threshold rejection, missing queue telemetry,
fault detection/partial retention, interruption/repaint restoration, and no
new workload after an idle fault. README documents the changed acceptance rule.

Round-5 candidate validation: terminal/wrapper tests, all 13 Python tests,
Python byte compilation, host benchmarks, both target builds/packages with
built-image byte equality, and `git diff --check` passed. These checks prepare
the nonzero-tick candidate; they do not establish its watchdog/runtime safety.

- Round-5 `tpager`: 1,770,160 bytes, SHA-256 `75f49d7a46db8bf9a221a2c4794e32acb0d80ad63a64c56ba8ade6550a181ffb`.

- Round-5 `tdeckplus`: 4,247,504 bytes, SHA-256 `9eba22ac8cccb7657ccef4cdce3b43d0c81667ce91e66365f0bb0247fc8c949a`.
