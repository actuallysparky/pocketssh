# PocketSSH T-Pager / T-Deck Plus Variant

This repository is a focused fork of the original PocketSSH project:

- Source project: https://github.com/0015/PocketSSH
- Original authors and project maintainers retain full credit for PocketSSH.

This variant focuses on the LilyGO T-Lora Pager / T-Pager hardware, while preserving the upstream T-Deck Plus baseline and carrying the shared SSH/Wi-Fi/profile UX forward.

## v1.1 Highlights (T-Lora Pager)
- T-Pager hardware bring-up and clean firmware packaging.
- OpenSSH-style host aliases from `/sdcard/ssh_keys/ssh_config`:
  - `connect <alias>`
  - `hosts`
- Stored Wi-Fi profiles from `/sdcard/ssh_keys/wifi_config`:
  - `wifi`
  - `wifi <Network|SSID>`
  - `wifi auto`
- Status bar context improvements:
  - active Wi-Fi SSID
  - connected SSH host/IP
- Terminal font-size mode:
  - `fontsize` toggle
  - `fontsize big|normal`
  - default from `ssh_config` via `fontsize`
- Encoder interaction model for no-touch T-Lora Pager hardware:
  - encoder: command history
  - `Alt` + encoder: input cursor left/right
  - `Caps` + encoder: terminal buffer scroll up/down
- Power command:
  - `shutdown` / `poweroff` enters deep sleep (wake by BOOT or encoder button)

## Build/Deploy Contract (T-Pager)
- Packaged artifact name is `PocketSSH-TPager.bin`.
- Every install cycle must also stage the same build to SD at `/sdcard/PocketSSH-TPager.bin`.
- Recommended host flow:
  1. `idf.py -B /Users/sparky/engineering/_state/esp/_local/build/pocketssh/tpager -DTPAGER_TARGET=ON -DTPAGER_DIAG=OFF build`
  2. `./misc/package_tpager_bin.sh`
  3. `~/.espressif/python_env/idf5.5_py3.14_env/bin/python ./misc/serial_push_bin.py --port /dev/cu.usbmodem201101`

## Build/Deploy Contract (T-Deck Plus)
- Packaged artifact name is `PocketSSH-TDeckPlus.bin`.
- Recommended host flow:
  1. `idf.py -B /Users/sparky/engineering/_state/esp/_local/build/pocketssh/tdeckplus build`
  2. `./misc/package_tdeckplus_bin.sh`
  3. Inspect Launcher app slots before flashing:
     `python3 ./misc/flash_app_partition.py --port /dev/cu.usbmodemXXXX --list`
  4. Flash only a confirmed Launcher-managed app/OTA slot:
     `python3 ./misc/flash_app_partition.py --port /dev/cu.usbmodemXXXX --bin /Users/sparky/engineering/_state/esp/_local/packages/pocketssh/PocketSSH-TDeckPlus.bin --partition ota_0`
- Fallback SD staging flow after PocketSSH is running:
  `python3 ./misc/serial_push_bin.py --target tdeckplus --port /dev/cu.usbmodemXXXX`

Never flash the factory partition when iterating under Launcher; keep Launcher recoverable.
Shared Launcher-safe deployment rules live in
`../tpager-launcher/tpager-launcher-deployment.md`.

For full project features, documentation, and history, see the upstream repository above.
