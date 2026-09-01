# PocketSSH 2.0-dev T-Pager target

The `TPAGER_TARGET` build shares the PocketSSH 2.0 terminal core, ANSI
renderer, SSH trust policy, profile handling, and reconnect behavior with the
T-Deck Plus. It uses the T-Pager's existing display, keyboard, SD, and encoder
bring-up path.

## Physical controls

- Orange **Symbols** plus counter-clockwise dial: show terminal controls
  (the T-Deck left-swipe action).
- Orange **Symbols** plus clockwise dial: show configured SSH servers
  (the T-Deck right-swipe action).
- With either sheet open, turn the dial to move the focus ring and press the
  dial center to activate the selected control or server.
- Symbols by itself remains the normal momentary symbol/number keyboard chord.
- `Alt` plus dial moves the local input cursor; `Caps` plus dial scrolls
  terminal output; an unmodified dial navigates local command history whenever
  no sheet is open.

## Build and artifact

Build the target with an isolated ESP-IDF build directory:

```sh
idf.py -B /path/to/build/tpager-2.0 -DTPAGER_TARGET=ON -DTPAGER_DIAG=OFF build
```

The T-Pager profile uses 8 MiB QSPI PSRAM. ESP-IDF calls that electrical bus
mode `QUAD`; its generated `sdkconfig` must show `CONFIG_SPIRAM_MODE_QUAD=y`
and `CONFIG_SPIRAM_TYPE_ESPPSRAM64=y`. The T-Deck Plus profile is kept
separate and continues to select octal PSRAM. Do not reuse a build directory
between the targets.

Package the resulting app with `misc/package_tpager_bin.sh`. The established
T-Pager package and SD-sidecar filename remains `PocketSSH-TPager.bin`.

This development note documents source behavior only. It is not a release,
does not authorize a flash, and does not replace the T-Deck Plus Launcher-safe
`PocketSSH-2.0.bin` artifact contract.
