# PocketSSH Pagerdeck release

Target: LilyGO T-Deck Plus, for use with the Pagerdeck/Launcher app-slot
workflow.

Source revision: `fa6b98d` (the current checkout has only later journal/history
updates after this build).

## Files

- `PocketSSH-2.0.bin` — application image
- `SHA256SUMS.txt` — integrity manifest

Verify the binary before staging or installing it:

```bash
shasum -a 256 -c SHA256SUMS.txt
```

This is an application image for the compatible T-Deck Plus partition layout.
Use the Launcher-managed app slot only; do not use it as a full-device
replacement image.
