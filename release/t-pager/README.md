# PocketSSH T-Pager release

Target: LilyGO T-Pager / T-LoRa Pager.

Source revision: `fa6b98d` (the current checkout has only later journal/history
updates after this build).

## Files

- `PocketSSH-TPager.bin` — application image
- `SHA256SUMS.txt` — integrity manifest

Verify the binary before staging or installing it:

```bash
shasum -a 256 -c SHA256SUMS.txt
```

This is an application image for the compatible T-Pager partition layout. Do
not use it as a full-device replacement image.
