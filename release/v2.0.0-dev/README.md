# PocketSSH 2.0 development build evidence

This record identifies the firmware artifact built from source revision
`12f2c5b` (`Request initial PTY at canvas geometry`).  It is a development
candidate, not a release tag.

## Artifact

- Package name: `PocketSSH-2.0.bin`
- SHA-256: `85724c3ff7d958b6f55cb16f0877b65455d8eaf868efe67e11c73f0720f334c4`
- Size: `4,126,880` bytes
- Build result: fits the 9 MiB app partition with 56% free space

## Launcher-safe delivery evidence

- Registered T-Deck Plus identity: `20:6e:f1:a5:59:30`
- Verified target partition: `app1` / `ota_0` at `0x200000`, size `0x800000`
- SD sidecar: `/sdcard/PocketSSH-2.0.bin`
- SD transfer verification: `4,126,880` bytes, CRC32 `10ad7117`
- The flash operation wrote only the verified app partition.  The Launcher,
  factory, bootloader, partition table, NVS, and existing SD ROM were not
  written.

## Validation completed

- Host-native terminal-core test suite passed.
- Clean ESP-IDF T-Deck Plus build passed.
- Device booted, joined the configured Wi-Fi network, loaded the persistent
  host-key cache, authenticated to `prodmini`, and opened an SSH shell.
- Initial PTY log: `TERM=xterm-256color`, `45x20` cells, `320x180` pixels.
- ANSI/alternate-screen, `top`, `vim -c q`, and interactive `less` quit-key
  smoke paths were exercised through the SSH channel.
- Ten-minute active-session soak completed with 636/636 ICMP replies and no
  observed reset or Wi-Fi interruption.

## Release acceptance still requiring direct evidence

The functional specification requires a changed-host-key rejection test and
human-visible display checks for the final canvas presentation, selection,
paste, and overlay gestures.  Do not create a final `v2.0.0` release tag from
this development evidence alone.

## Latest development milestone (2026-08-31)

This newer milestone is built from source revision `96df71f` (`Show terminal
copy feedback visibly`).  Copy and selection now show a timed canvas-overlay
notice such as `Copied 512 B` or `Copied 4096 B (truncated)` instead of
writing feedback into the hidden local textarea during an SSH session.

- Package name: `PocketSSH-2.0.bin`
- SHA-256: `2378a44e31604236f3660aa16cf45f36334ab323384728c1f63d3600cc0283d7`
- Size: `4,127,568` bytes
- Host terminal-core tests and the isolated T-Deck Plus ESP-IDF build passed.
- The registered device (`20:6e:f1:a5:59:30`) was flashed only at `app1` /
  `ota_0`, `0x200000`, with the same SHA-256 artifact.  The 8 MiB app slot
  retains 56% free space; no Launcher, factory, bootloader, partition table,
  NVS, or existing SD ROM was written.
- Boot evidence includes the 512-line PSRAM scrollback allocation, SD config
  and `known_hosts` cache loading, Wi-Fi association, and an authenticated
  `prodmini` shell with an initial `xterm-256color` 45x20 PTY.
- A ten-minute untouched SSH-session soak was completed.  USB-JTAG serial
  monitoring resets this target, so the session was deliberately not probed
  during the interval; it is soak-duration evidence, not an independently
  observed post-soak channel-health assertion.

SD sidecar status for this exact artifact is intentionally **unverified**.
The receiver acknowledged `/sdcard/PocketSSH-2.0.bin` and began the transfer,
but the local serial transport lost the sender's final completion record.
A later read-only SD mount check confirmed the card and existing configuration
remain accessible, but this build does not serial-log directory sizes or CRCs.
Do not claim the sidecar's contents are the latest artifact until a future
transfer emits `POCKETCTL serialrx_complete` with the matching size and CRC32.
