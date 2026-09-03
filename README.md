# PocketSSH

PocketSSH is a focused ESP-IDF fork of [0015/PocketSSH](https://github.com/0015/PocketSSH), maintained as one SSH-terminal application for LilyGO T-Pager and T-Deck Plus hardware. Shared terminal, SSH, Wi-Fi-profile, SD-transfer, and asset code lives here; target differences are selected at build time.

## Targets and output

| Target | Build command | Package command | Canonical artifact |
| --- | --- | --- | --- |
| T-Pager | `./build.sh tpager` | `./package.sh tpager` | `_local/packages/tpager/PocketSSH-TPager.bin` |
| T-Deck Plus | `./build.sh tdeckplus` | `./package.sh tdeckplus` | `_local/packages/tdeckplus/PocketSSH-2.0.bin` |

The wrappers derive the repository root, default `IDF_PATH` to the engineering ESP-IDF toolchain, and keep each target's generated configuration in its own ignored `_local/build/<target>` directory. `POCKETSSH_BUILD_DIR` and `POCKETSSH_PACKAGE_DIR` may override those locations for an explicitly isolated build.

Run host-native terminal tests with:

```bash
./tests/run_terminal_core_tests.sh
```

Legacy packaging entry points under `misc/` remain as compatibility wrappers around `package.sh`.

## Delivery boundary

T-Pager packages retain the `PocketSSH-TPager.bin` contract; T-Deck Plus packages retain `PocketSSH-2.0.bin`. Any SD staging or Launcher app-slot write remains a separate, identity-gated operation: inspect the live partition table immediately before a flash, write only the confirmed Launcher-managed application slot, and never replace the factory or Launcher partition during iteration.

Building and packaging do not establish device runtime acceptance.

## T-Pager streaming benchmark

The T-Pager exposes content-free terminal timing snapshots through its existing
USB control channel:

```text
__pocketctl perf reset
__pocketctl perf snapshot
```

Use `misc/perf_benchmark.py --port <usb-port>` to run three 512 KiB scrolling
trials against an already connected POSIX SSH shell. If opening USB resets the
Pager, pass `--connect-command 'connect <saved-alias>'`; that command is never
written to the benchmark summary, and direct credentials are rejected. Each
snapshot includes receive-to-invalidation p95, draw/lock timing, and heap
headroom. Results and raw serial logs are stored only under ignored
`_local/benchmarks/`. Benchmarking, like flashing, remains separate from source
validation and requires the normal device identity gate. A timeout is recorded
as a partial, content-free trial in `summary.json` and returns a nonzero exit
status; it does not discard the final telemetry sample.

Each snapshot also emits one paired `POCKETCTL core` line with content-free
parser and scroll operation counts. `perf_benchmark.py --workload` accepts
only fixed `screen-fill`, `ascii-scroll`, and `ansi-utf8-scroll` workloads;
it never accepts arbitrary remote commands. Run deterministic host-only core
microbenchmarks with `./tests/run_terminal_core_benchmarks.sh`; results are
also ignored under `_local/benchmarks/host/`.

The paired `POCKETCTL transport` line adds socket-poll, channel-EAGAIN, flush,
and display-update timing counters. For lab diagnosis only, T-Pager supports
`__pocketctl perf repaint normal|deferred`; deferred mode continues SSH and
terminal-core processing but defers receive-loop repainting. The benchmark
helper exposes it as `--repaint-mode` and always restores normal mode after a
trial or timeout. It is not an interactive terminal setting.

## Documentation

- `docs/pocketssh-2.0-functional-spec.md` — T-Deck Plus terminal behavior and delivery contract.
- `docs/pocketssh-2.0-implementation-requirements.md` — implementation requirements.
- `docs/enhancements-requirements.md` — stored-host, Wi-Fi-profile, and known-host expectations.
- `docs/project-plan.md` — original T-Pager porting plan and hardware rationale.
- `journal.md` — append-only engineering and delivery evidence.

Original upstream authors and maintainers retain full credit for PocketSSH.
