# PocketSSH release bundles

These directories contain the two public application images prepared from the
current PocketSSH source tree. Each bundle includes a target README and a
SHA-256 manifest.

- `t-pager/` — LilyGO T-Pager, `PocketSSH-TPager.bin`
- `pagerdeck/` — LilyGO T-Deck Plus / Pagerdeck, `PocketSSH-2.0.bin`

The images are application binaries for an existing compatible partition
layout. They are not full-device images and do not replace a bootloader,
partition table, or Launcher installation.

The build-relevant source corresponds to local revision `fa6b98d`; later
PocketSSH commits through `a7e4d50` only record validation and delivery history.
