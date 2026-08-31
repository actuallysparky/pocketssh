# PocketSSH Enhancements — Stored Hosts, Network Profiles, and Known Hosts

> **2.0 status:** This document remains authoritative for the existing
> `ssh_config` and `wifi_config` compatibility contract. The terminal,
> host-key, input, and Launcher-delivery requirements for the 2.0 rewrite are
> defined by `pocketssh-2.0-functional-spec.md` and
> `pocketssh-2.0-implementation-requirements.md`.

## Overview
This enhancement set adds:
1. **Stored SSH connections** using an OpenSSH-style client config file.
2. **Stored Wi‑Fi network profiles**, optionally linked to SSH hosts for “connect and go”.
3. **UI tweaks**
3. **Known hosts** support (host key trust + persistence), to improve security and UX.

Design goals:
- Keep the SSH config file as close to **OpenSSH `ssh_config`** semantics as practical.
- Keep PocketSSH conventions: SSH private keys remain in **`/sdcard/ssh_keys/`**.
- Avoid a heavy settings UI; prefer “configuration by file” with light UI affordances.

---

## 1) Stored SSH Connections (OpenSSH-style `ssh_config`)

### 1.1 File location
- SSH config file: **`/sdcard/ssh_keys/ssh_config`**
- Key files: **`/sdcard/ssh_keys/*`** (existing PocketSSH pattern)

### 1.2 Supported syntax
Support a useful subset of OpenSSH client config.

**Required / high-value directives**
- `Host` (aliases, support `*` wildcard)
- `HostName`
- `User`
- `Port`
- `IdentityFile`
- `IdentitiesOnly`

**Nice-to-have (recommended if easy)**
- `ConnectTimeout`
- `ServerAliveInterval`
- `ServerAliveCountMax`
- `StrictHostKeyChecking` (mapped to PocketSSH behavior; see Known Hosts section)

Unsupported directives should be ignored (do not error).

### 1.3 Path expansion rules
To improve compatibility with copied configs:
- Expand `IdentityFile ~/.ssh/<file>` as:
  - `~/.ssh/` → **`/sdcard/ssh_keys/`**
- Also accept absolute paths under `/sdcard/ssh_keys/…`.

Examples:
- `IdentityFile ~/.ssh/ha_ed25519` → `/sdcard/ssh_keys/ha_ed25519`
- `IdentityFile /sdcard/ssh_keys/ha_ed25519` → unchanged

### 1.4 Resolution and precedence (compatibility behavior)
Implement predictable OpenSSH-like semantics:
1. Parse the config file top-to-bottom.
2. Apply any global options before the first `Host`.
3. For a given connect target, apply all matching `Host` blocks in order:
   - last value wins for scalar options (`HostName`, `User`, `Port`, `IdentitiesOnly`)
   - list-like options may accumulate (multiple `IdentityFile` entries)
4. Ignore unknown keys.

### 1.5 Example `ssh_config` entry
```
Host ha
  HostName citadeloftruth.duckdns.org
  User hassio
  Port 2222
  IdentityFile ~/.ssh/ha_ed25519
  IdentitiesOnly yes
  Network home
```

> `Network` is an extension directive used to link a host to a Wi‑Fi profile (defined below).

---

## 2) Stored Wi‑Fi Network Profiles

### 2.1 Separate Wi‑Fi config file (recommended)
To keep `ssh_config` aligned with standard OpenSSH semantics, store Wi‑Fi networks in a separate file.

- Wi‑Fi networks file: **`/sdcard/ssh_keys/wifi_config`**

This file uses a simple block format:
```
Network home
  SSID mywifiap
  Password mysupersecretpassword
  AutoConnect true
  Priority 10
```

### 2.2 Wi‑Fi profile fields
- `Network <name>`: identifier used by `TPagerNetwork` in `ssh_config`.
- `SSID`: literal SSID string.
- `Password`: WPA2/WPA3 password (or PSK).
- `AutoConnect`: `true|false`.

Unknown fields should be ignored. If multiple AutoConnect: true fields exist then the first one in the file (from the top down) will 'win'. If that one fails to connect for some reason, it will continue down the list of autoconnect APs and try to connect until one does.

### 2.3 How Wi‑Fi profiles are consumed
The Wi‑Fi profiles should be usable in three ways.

#### A) Host-linked Wi‑Fi auto-connect
- If a `Host` entry contains `Network <name>`:
- When the user runs `connect <HostAlias>` and the device is not connected (or the connection is failing), PocketSSH attempts to connect to the matching Wi‑Fi profile first.
- If already connected to Wi‑Fi, do not switch networks unless the SSH connection fails.

#### B) SSID quick-connect
If the user runs a command like:
- `wifi <SSID-or-NetworkName>`

Then:
- If an entry exists in `wifi_config` whose `Network` matches, use it.
- Else if an entry exists whose `SSID` matches, use it.
- If a password exists, use it automatically.
- Otherwise prompt for a password (optional; depends on UI support).

#### C) Boot-time AutoConnect
On boot:
1. Read `wifi_config`.
2. Find all entries with `AutoConnect true`.
3. Sort by0 file order (descending).
4. Scan for SSIDs and connect to the first available matching network.

---

## 3) UI tweaks
1. When connected to wifi, display the SSID name to the right of the wifi icon in the titelbar
2. When connectede to a host, display the host IP or hostname to the right of the <checkmark> SSH
3. Replace touchscreen scrubbing requirements with encoder-modifier controls (T-Lora Pager has no touch input on this build target):
   - Encoder (default): navigate command history up/down.
   - Encoder + Alt (hold): move command-line cursor right/left for in-line editing.
   - Encoder + Caps (hold): scroll terminal output buffer up/down.
   - If both Alt and Caps are held, Caps behavior (buffer scroll) wins.
4. Add support for a bigger font at the expense of screen real estate. By including the key fontsize big in the config file, the default 67 x 13 will render at a larger size and be 53 x 9 instead. At runtime the user can type fontsize when not in an SSH session to toggle between the two sizes. If fontsize normal is added in the config file then the default smaller font will be used (which is silly, only declaring big makes sense, but symmerty is importan)


## 4) Known Hosts (Host Key Trust + Persistence) — New Contribution

PocketSSH currently lacks known-hosts functionality. This enhancement adds it to improve security, match user expectations, and make the project more “real SSH client”.

### 4.1 File location
- Known hosts file: **`/sdcard/ssh_keys/known_hosts`**

### 4.2 Behavior
On SSH connect:
1. Retrieve the server host key and compute a fingerprint (at least SHA256 form).
2. Check for a matching entry in `known_hosts`.

Cases:

#### A) First-time host (no entry)
Prompt user:
- Show host identifier (`HostName:Port`) and fingerprint.
- Options:
  - **Accept and save**
  - Accept once (optional)
  - Reject

Default recommendation: **Accept and save**.

#### B) Known host matches
Connect without prompting.

#### C) Known host key changed
Prompt user with a strong warning:
- Show old fingerprint and new fingerprint.
- Options:
  - Reject (default)
  - Replace and save (explicit confirmation)

### 4.3 Format
Preferred: follow OpenSSH `known_hosts` style where feasible, but a minimal custom format is acceptable if easier.

Minimum required fields:
- host (hostname or IP)
- port (if non-default)
- key type
- key material **or** fingerprint (key material preferred to verify accurately)

### 4.4 Integration with `StrictHostKeyChecking`
If `StrictHostKeyChecking` is supported in `ssh_config`, map it as:
- `yes`: reject unknown hosts; reject changed hosts
- `ask` (default): prompt and allow save
- `no`: auto-accept unknown hosts (still warn on key changes)

If this directive is not implemented, default behavior should be equivalent to `ask`.

---

## 5) Command / UX Expectations (minimal)
- `connect <alias>`:
  - resolve `<alias>` via `/sdcard/ssh_keys/ssh_config`
  - if `TPagerNetwork` is present and not connected, connect Wi‑Fi first
  - then perform SSH connect and shell start
- Optional:
  - `hosts`: list parsed `Host` aliases
  - `wifi`: list known `Network` entries
  - `wifi <name|ssid>`: connect using stored credentials (if present)
- When adding a feature document it in the readme,md and call it out as an enhancement to the OG project. Continue to give the initial project full credit for all the work they did and celebrate the foundation we're building on.

---

## 6) Security Notes (pragmatic)
- Private keys and Wi‑Fi passwords are stored on SD card for convenience.
- Do not print secrets (Wi‑Fi password, private key contents) to serial logs.
- Encourage passphrase-protected keys if feasible (optional future UI for passphrase entry).

---

## 7) Implementation Notes (for maintainability)
- Keep parsing tolerant:
  - ignore unknown directives/fields
  - allow extra whitespace
  - allow comments beginning with `#`
- Keep files in one place to mirror PocketSSH:
  - `/sdcard/ssh_keys/ssh_config`
  - `/sdcard/ssh_keys/wifi_config`
  - `/sdcard/ssh_keys/known_hosts`
- Prefer incremental implementation:
  1. `ssh_config` parsing + host list + connect by alias
  2. `wifi_config` parsing + boot autoconnect + host-linked network
  3. `known_hosts` prompts + persistence + change detection
