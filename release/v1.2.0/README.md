# PocketSSH v1.2.0 Source Baseline

`v1.2.0` is the immutable source baseline for the PocketSSH 2.0 terminal
rewrite. It records the existing T-Pager / T-Deck Plus variant exactly as
versioned by `PROJECT_VER` before 2.0 development begins.

## Deliberate scope

- Source tag only: this baseline does not add rebuilt firmware artifacts.
- No device was flashed and no SD-card artifact was created for this release.
- Existing T-Deck Plus Launcher and its installed application remain unchanged.

The follow-on `pocketssh-2.0` branch owns the new terminal implementation and
will use its own `PocketSSH-2.0.bin` artifact contract.
