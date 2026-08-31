# PocketSSH 2.0 Functional Specification

## Product outcome

PocketSSH 2.0 turns the T-Deck Plus build from a line-oriented SSH console
into a compact, traditional interactive terminal. It remains a
Launcher-compatible application: the Launcher is never replaced, and factory,
partition-table, NVS, and storage regions are never written.

The T-Pager target remains buildable and retains its existing input/profile
workflow. The 2.0 terminal work is validated on T-Deck Plus first.

## User-facing modes

### Local mode

When no SSH channel is active, PocketSSH presents the existing local prompt
and retains `wifi`, `hosts`, `connect`, `ssh`, `sshkey`, `fontsize`, `/color`,
`serialrx`, `netinfo`, `clear`, and `exit` behavior. Existing SD paths,
profiles, NVS preferences, command history, status bar, and battery display
remain compatible.

### Remote terminal mode

When an SSH shell opens, printable and control input is written to the SSH
channel immediately; it is not accumulated into the local prompt. A remote
screen replaces the prompt output area while preserving the status bar. The
user reaches local controls with either a long trackball press or `Ctrl+]`.

Both invoke the same non-destructive overlay. It offers disconnect, reconnect,
scrollback navigation, copy, paste, font-size selection, and connection state.
Closing it returns focus to the remote terminal without changing the remote
screen.

## Terminal compatibility contract

PocketSSH requests `TERM=xterm-256color`. Its initial PTY dimensions are the
actual drawable terminal cell columns/rows and pixels. A font-size or layout
change sends an SSH window-size update before rendering continues.

The terminal model owns separate normal and alternate screens, cursor state,
autowrap, origin mode, scroll region, saved cursor state, tab stops,
application-cursor mode, bracketed-paste mode, and bounded normal-screen
scrollback. Unsupported private sequences are bounded and ignored safely;
they never print their bytes or mutate device configuration.

### Required input bytes

The input mapper supports printable ASCII, UTF-8 paste, Backspace, Enter, Tab,
Escape, arrows, Home, End, Page Up, Page Down, Insert, Delete, F1 through
F12, and Ctrl/Alt-modified variants that the hardware keyboard can report.
Arrow keys honor application-cursor mode. The overlay and local prompt never
replace these remote key bindings while a channel is active.

Pasting sends raw text normally. If the remote terminal has enabled bracketed
paste, PocketSSH wraps the same text in `CSI 200~` and `CSI 201~`. The
device-local clipboard is RAM-only, limited to 4 KiB, and is cleared at
disconnect/reboot; it is never written to NVS or SD.

### Required output behavior

The parser is streaming: any byte boundary, including a boundary inside an
escape sequence or UTF-8 code point, has the same result as receiving the
whole sequence at once.

- C0 controls: BEL is non-rendering; BS, HT, LF, CR, and FF perform terminal
  actions.
- CSI: cursor movement/positioning, erase display/line, insert/delete
  character and line, scroll up/down, scroll region, save/restore cursor,
  cursor visibility, and SGR are supported.
- DEC/xterm behavior: alternate-screen enter/leave, origin mode, autowrap,
  application cursor keys, and bracketed paste are supported.
- OSC and unsupported sequences are bounded and ignored safely. Remote OSC
  commands may not change the status bar, theme, or other device settings.

SGR supports reset; bold/bright; dim; underline; inverse; conceal;
strikethrough; normal and bright ANSI colors; 256-color foreground/background;
and default foreground/background. Bold renders as the bright color when the
bitmap font has no bold face. Truecolor input is quantized to the nearest
xterm 256-color palette entry. Unsupported UTF-8 glyphs render as one
replacement cell without desynchronizing the grid.

The default foreground after reset is the user-selected PocketSSH theme color;
the default background is black. Remote SGR overrides those defaults.

### Scrollback and rendering

Normal-screen scrollback defaults to 512 complete terminal rows in PSRAM. If
allocation fails, PocketSSH retries at 128 then 64 rows and remains usable.
Alternate-screen content is never added to scrollback. Scrolling backward
freezes the viewed content; new output does not force the viewport to the
bottom. Returning to the bottom resumes live following.

PocketSSH uses a monospaced terminal font and cell renderer. Rendering updates
only changed cells/rows; it does not create one LVGL object per character or
replace the complete output text for every SSH read.

## SSH host trust and session reliability

Known-host data lives at `/sdcard/ssh_keys/known_hosts`. Each entry records
the hostname, port, host-key type, and key material. Writes use a temporary
file plus rename so a power loss cannot replace a valid file with a partial
entry.

On first contact, PocketSSH displays the host, port, key type, and SHA-256
fingerprint. The user may Accept and save, Accept once, or Reject. A changed
key is rejected by default and can be replaced only by explicit
Replace-and-save confirmation. `StrictHostKeyChecking yes` rejects unknown and
changed keys; `ask` is the default prompt behavior; `no` accepts unknown keys
without persistence but still requires confirmation before replacing a changed
key.

Configured `ServerAliveInterval` and `ServerAliveCountMax` drive SSH
keepalives. A failed session shows its reason and exposes Reconnect through
the local overlay. Reconnect reuses the resolved profile but repeats host-key
validation; it never silently accepts a different key.

## Artifact and device-delivery contract

Every 2.0 T-Deck Plus package is named `PocketSSH-2.0.bin`. Before every
milestone flash, that exact byte stream is staged as
`/sdcard/PocketSSH-2.0.bin`; `/sdcard/PocketSSH-TDeckPlus.bin` and any other
existing ROM remain untouched. Transfer success is verified with the reported
size and CRC.

Only a device whose identity has passed the registered-device gate may be
flashed. The live partition table must be read immediately before a flash and
must identify `ota_0` as a Launcher-managed application slot. Only that
confirmed slot is written.

## Acceptance criteria

2.0 is ready for release-candidate flashing only when it builds cleanly,
passes host-native terminal tests, fits the confirmed OTA slot, stages the
same SHA-256 artifact to SD, and demonstrates on the T-Deck Plus:

1. first-use, matching, and changed-host-key behavior;
2. 16/256-color output and CR/cursor-addressed redraw;
3. direct interactive input in `less`, `vim`, and a live-refresh application;
4. alternate-screen restore, modifier keys, resize, scrollback, and paste;
5. explicit reconnect after a recoverable Wi-Fi interruption; and
6. a 30-minute active session without a crash, reset, unbounded memory loss,
   or UI deadlock.

SFTP/SCP, multiple concurrent sessions, port forwarding, and SSH-agent
support are explicitly post-2.0 work.
