# PocketSSH 2.0 Implementation Requirements

## Delivery order and approval gate

This branch begins at source tag `v1.2.0` and declares `2.0.0-dev`. No
terminal-code, package, build, SD-stage, or flash change may begin until the
functional specification commit is explicitly approved by the product owner.

After approval, implementation proceeds in these ordered milestones:

1. host-testable terminal core and tests;
2. T-Deck Plus renderer, PTY geometry, and direct input integration;
3. known-host, keepalive, reconnect, and overlay behavior;
4. package/stage tooling and terminal-core device milestone;
5. release-candidate build, OTA-slot delivery, and full device validation.

## Module boundaries

Add a portable C++ terminal-core module with no ESP-IDF, LVGL, FreeRTOS, or
libssh2 dependency. Its public surface shall provide:

- construction/resizing with cell columns and rows;
- `feed(bytes, length)` for the received SSH stream;
- normal/alternate screen and viewport accessors;
- dirty row/cell retrieval and acknowledgment;
- terminal-mode accessors needed by input encoding; and
- a key encoder that returns exact SSH bytes for a normalized key event.

Each cell carries a Unicode code point, foreground/background palette value,
style flags, and width. The core owns parser state and all terminal mutation;
the renderer never parses escapes and the SSH receive task never calls LVGL.

Keep host-specific terminal memory allocation in a narrow adapter. It must
prefer PSRAM, then reduce capacity as specified, and report allocation
degradation through the local UI/log without aborting a session.

## Renderer and input integration

Replace the `terminal_output` text-area append path with a single terminal
canvas/grid object. Use a fixed-cell font, calculate visible geometry from the
drawable rectangle, and draw dirty background, glyph, underline, and strike
regions under the existing LVGL display lock. Keep the status bar and local
input controls outside the renderer.

Introduce a normalized key-event layer between the T-Deck keyboard/trackball
drivers and SSH writes. It preserves existing local-mode editing while routing
remote-mode events immediately through the terminal-core encoder. Long
trackball press and `Ctrl+]` invoke the same overlay callback.

Selection converts selected grid cells to UTF-8 into the 4 KiB RAM clipboard.
Copy truncation is visible to the user. Paste is disabled when no remote
channel exists and respects bracketed-paste mode when enabled.

On SSH channel open, use the extended libssh2 PTY request with
`xterm-256color`, cell dimensions, and pixel dimensions. On geometry change,
use the corresponding libssh2 PTY-size request. Do not claim xterm support or
send a resize if either operation fails; show the failure in the local overlay.

## Host trust and I/O rules

Implement host-key verification after the SSH handshake and before password or
public-key authentication. Use the libssh2 host-key blob/type, SHA-256
fingerprint calculation, and atomic SD persistence. Parse existing
`StrictHostKeyChecking`, `ServerAliveInterval`, and `ServerAliveCountMax`
values; preserve their existing profile-resolution precedence.

Use a dedicated, bounded SSH transmit routine for normal keys, paste, overlay
commands, and keepalives. It handles `EAGAIN`, partial writes, disconnect, and
serialization so interleaved producer tasks cannot corrupt a sequence. Keep
display rendering and SD writes out of the receive loop.

## Tests and release evidence

Add a host-native test target with no ESP toolchain dependency. Its golden
tests cover byte-by-byte and randomized chunk delivery, C0/CSI/DEC operations,
SGR palette mapping, malformed/bounded sequences, UTF-8 fallback, alternate
screen, resize, scrollback fallback, selection, bracketed paste, and
normal/application key encoding.

After each milestone, run terminal-core tests, source whitespace checks, and a
fresh T-Deck Plus ESP-IDF build in the engineering state build directory. A
flash milestone additionally requires registered-device identity evidence,
live partition-table evidence, packaged SHA-256/image-size evidence, SD
transfer size/CRC evidence, and boot/runtime logs.

The package and serial-stage helpers must expose an explicit 2.0 T-Deck Plus
mode whose default local and remote filename is `PocketSSH-2.0.bin`. Staging
occurs before flashing and refuses any path/name that could overwrite the
existing T-Deck Plus ROM. The OTA helper receives the explicit packaged path
and `--partition ota_0`; it must not rely on a factory-partition default.
