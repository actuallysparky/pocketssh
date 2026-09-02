# PocketSSH T-Pager / T-Deck Plus Variant

This repository is a focused fork of the original PocketSSH project:

- Source project: https://github.com/0015/PocketSSH
- Original authors and project maintainers retain full credit for PocketSSH.

This variant focuses on the LilyGO T-Lora Pager / T-Pager hardware, while preserving the upstream T-Deck Plus baseline and carrying the shared SSH/Wi-Fi/profile UX forward.

On T-Deck Plus, the supplied PocketSSH GIF is retained in the asset pack; its
first frame is rendered as the startup splash because the full LVGL GIF decoder
starves the device UI task and triggers the watchdog.

## v1.1 Highlights (T-Lora Pager)
- T-Pager hardware bring-up and clean firmware packaging.
- OpenSSH-style host aliases from `/sdcard/ssh_keys/ssh_config`:
  - `connect <alias>`
  - `hosts`
- Direct SSH is always available; a saved alias/key is optional:
  - `ssh <hostname-or-ip> [port]` prompts for username then a masked,
    RAM-only password.
  - `ssh <host> <port> <user> <password>` accepts an explicit password form
    with the password redacted from on-screen command echo and excluded from
    command history.
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
- Local serial SD access:
  - `serialrx <relative-path>` writes any SD-relative file path, creates
    missing parent directories, and uses size/CRC verification plus atomic
    promotion. `sdverify <relative-path>` reports the resulting size and CRC.
  - `serialtx <relative-path>` reads an SD file to the local serial console;
    `misc/serial_pull_bin.py` receives it and creates a local file only after
    the device size and CRC agree.
  - Absolute paths, `.`/`..` traversal, backslashes, and control characters
    remain rejected. This is a physical local-console trust boundary: a local
  operator could otherwise remove and modify the card directly.

## Build/Deploy Contract (T-Pager)
- Packaged artifact name is `PocketSSH-TPager.bin`.
- Every install cycle must also stage the same build to SD at `/sdcard/PocketSSH-TPager.bin`.
- Recommended host flow:
  1. `idf.py -B /Users/sparky/engineering/_state/esp/_local/build/pocketssh/tpager -DTPAGER_TARGET=ON -DTPAGER_DIAG=OFF build`
  2. `./misc/package_tpager_bin.sh`
  3. `~/.espressif/python_env/idf5.5_py3.14_env/bin/python ./misc/serial_push_bin.py --port /dev/cu.usbmodem201101`

## Build/Deploy Contract (T-Deck Plus)
- Packaged artifact name is `PocketSSH-2.0.bin`.
- Stage the same artifact to `/sdcard/PocketSSH-2.0.bin`; leave the existing
  Launcher ROM, including `PocketSSH-TDeckPlus.bin`, untouched.
- Recommended host flow:
  1. `idf.py -B /Users/sparky/engineering/_state/esp/_local/build/pocketssh/tdeckplus build`
  2. `./misc/package_tdeckplus_bin.sh`
  3. Inspect Launcher app slots before flashing:
     `python3 ./misc/flash_app_partition.py --port /dev/cu.usbmodemXXXX --list`
  4. Flash only a confirmed Launcher-managed app/OTA slot:
     `python3 ./misc/flash_app_partition.py --port /dev/cu.usbmodemXXXX --bin /Users/sparky/engineering/_local/packages/pocketssh/PocketSSH-2.0.bin --partition ota_0`
- Fallback SD staging flow after PocketSSH is running:
  `python3 ./misc/serial_push_bin.py --target tdeckplus --port /dev/cu.usbmodemXXXX`

Never flash the factory partition when iterating under Launcher; keep Launcher recoverable.
Shared Launcher-safe deployment rules live in
`../tpager-launcher/tpager-launcher-deployment.md`.

For full project features, documentation, and history, see the upstream repository above.
