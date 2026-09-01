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
