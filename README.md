# PocketSSH

PocketSSH is an ESP-IDF SSH terminal for LilyGO T-Pager and T-Deck Plus
hardware. It keeps both targets in one source tree and selects the target at
build time.

## Build

```bash
./tests/run_terminal_core_tests.sh
./build.sh tpager
./package.sh tpager
./build.sh tdeckplus
./package.sh tdeckplus
```

The wrappers place generated files under ignored `_local/`. They require an
ESP-IDF 5.x environment; set `IDF_PATH` when it is not at the engineering
workspace default.

## Included releases

The checked-in release bundles contain the application binaries and SHA-256
manifests:

- [`release/t-pager/`](release/t-pager/) — `PocketSSH-TPager.bin`
- [`release/pagerdeck/`](release/pagerdeck/) — `PocketSSH-2.0.bin` for T-Deck Plus

## Documentation

- [`docs/pocketssh-2.0-functional-spec.md`](docs/pocketssh-2.0-functional-spec.md)
- [`docs/pocketssh-2.0-implementation-requirements.md`](docs/pocketssh-2.0-implementation-requirements.md)

## Credits and licenses

PocketSSH is forked from [0015/PocketSSH](https://github.com/0015/PocketSSH);
its authors and maintainers retain full credit for the upstream project.

The Pagerdeck/T-Deck Plus delivery workflow is designed for
[bmorcelli/Launcher](https://github.com/bmorcelli/Launcher), whose authors
retain credit for Launcher. This repository does not include Launcher itself.

The firmware also uses [Espressif ESP-IDF](https://github.com/espressif/esp-idf),
[esp-bsp](https://github.com/espressif/esp-bsp),
[LVGL](https://github.com/lvgl/lvgl), and
[libssh2_esp](https://github.com/skuodi/libssh2_esp); see `dependencies.lock` and
the component manifests for the pinned dependencies and their licenses.

This repository's license is in [`LICENSE`](LICENSE).
