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

## Documentation

- `docs/pocketssh-2.0-functional-spec.md` — T-Deck Plus terminal behavior and delivery contract.
- `docs/pocketssh-2.0-implementation-requirements.md` — implementation requirements.
- `docs/enhancements-requirements.md` — stored-host, Wi-Fi-profile, and known-host expectations.
- `docs/project-plan.md` — original T-Pager porting plan and hardware rationale.
- `journal.md` — append-only engineering and delivery evidence.

Original upstream authors and maintainers retain full credit for PocketSSH.
