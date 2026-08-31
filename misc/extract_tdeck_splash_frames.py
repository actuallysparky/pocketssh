#!/usr/bin/env python3
"""Predecode the supplied T-Deck GIF into LVGL RGB565 frames.

The ESP32-S3 GIF decoder can starve the UI task.  These native frames retain
the exact supplied animation but make each 100 ms update a pointer swap.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/PocketSSH_Splash_Pack/assets/t-deck/pocket_ssh_t-deck_320x240.gif"
OUTPUT = ROOT / "main/lvgl_pocketssh_img/pocketssh_splash_frames.rgb565"
WIDTH = 320
HEIGHT = 240
FRAME_COUNT = 16


def main() -> None:
    raw = bytearray()
    with Image.open(SOURCE) as image:
        if image.size != (WIDTH, HEIGHT) or image.n_frames != FRAME_COUNT:
            raise RuntimeError(f"unexpected splash GIF geometry/frames: {image.size}, {image.n_frames}")
        for frame_index in range(FRAME_COUNT):
            image.seek(frame_index)
            for red, green, blue in image.convert("RGB").getdata():
                rgb565 = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
                raw.extend(rgb565.to_bytes(2, "little"))
    OUTPUT.write_bytes(raw)
    expected = WIDTH * HEIGHT * 2 * FRAME_COUNT
    if len(raw) != expected:
        raise RuntimeError(f"wrong output size: {len(raw)} != {expected}")


if __name__ == "__main__":
    main()
