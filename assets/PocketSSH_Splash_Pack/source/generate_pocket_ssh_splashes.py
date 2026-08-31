#!/usr/bin/env python3
"""Generate native-resolution Pocket SSH splash screens for LILYGO devices."""

from __future__ import annotations

import math
import shutil
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter


ROOT = Path(__file__).resolve().parent
SOURCE_LOGO = ROOT / "generated_images" / "exec-6b2d1e0b-002f-434d-8c9e-9fd4d242bfd0.png"
OUT = ROOT / "PocketSSH_Splash_Pack"

BG = (0, 13, 5, 255)
DEEP = (0, 27, 10, 255)
FRAME = (7, 76, 31, 255)
DIM = (12, 139, 59, 255)
GREEN = (51, 255, 102, 255)
HOT = (182, 255, 188, 255)


FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "J": ("00111", "00010", "00010", "00010", "10010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
    "0": ("01110", "10011", "10101", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "10000", "11110", "00001", "00001", "11110"),
    "6": ("01110", "10000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    ">": ("10000", "01000", "00100", "00010", "00100", "01000", "10000"),
    "_": ("00000", "00000", "00000", "00000", "00000", "00000", "11111"),
    ".": ("00000", "00000", "00000", "00000", "00000", "00110", "00110"),
    "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
    "/": ("00001", "00010", "00010", "00100", "01000", "01000", "10000"),
    " ": ("00000",) * 7,
}


def text_size(text: str, scale: int, tracking: int | None = None) -> tuple[int, int]:
    tracking = scale if tracking is None else tracking
    return (len(text) * (5 * scale + tracking) - tracking, 7 * scale)


def pixel_text(
    image: Image.Image,
    xy: tuple[int, int],
    text: str,
    scale: int,
    fill: tuple[int, int, int, int],
    tracking: int | None = None,
) -> None:
    tracking = scale if tracking is None else tracking
    draw = ImageDraw.Draw(image)
    x0, y0 = xy
    cursor_x = x0
    for char in text.upper():
        glyph = FONT.get(char, FONT[" "])
        for row, bits in enumerate(glyph):
            for col, bit in enumerate(bits):
                if bit == "1":
                    draw.rectangle(
                        (
                            cursor_x + col * scale,
                            y0 + row * scale,
                            cursor_x + (col + 1) * scale - 1,
                            y0 + (row + 1) * scale - 1,
                        ),
                        fill=fill,
                    )
        cursor_x += 5 * scale + tracking


def centered_text(
    image: Image.Image,
    y: int,
    text: str,
    scale: int,
    fill: tuple[int, int, int, int],
    tracking: int | None = None,
) -> None:
    width, _ = text_size(text, scale, tracking)
    pixel_text(image, ((image.width - width) // 2, y), text, scale, fill, tracking)


def centered_text_in(
    image: Image.Image,
    x0: int,
    x1: int,
    y: int,
    text: str,
    scale: int,
    fill: tuple[int, int, int, int],
    tracking: int | None = None,
) -> None:
    width, _ = text_size(text, scale, tracking)
    pixel_text(image, (x0 + (x1 - x0 - width) // 2, y), text, scale, fill, tracking)


def chamfered_frame(image: Image.Image, inset: int, chamfer: int) -> None:
    draw = ImageDraw.Draw(image)
    w, h = image.size
    points = [
        (inset + chamfer, inset),
        (w - inset - chamfer - 1, inset),
        (w - inset - 1, inset + chamfer),
        (w - inset - 1, h - inset - chamfer - 1),
        (w - inset - chamfer - 1, h - inset - 1),
        (inset + chamfer, h - inset - 1),
        (inset, h - inset - chamfer - 1),
        (inset, inset + chamfer),
    ]
    draw.line(points + [points[0]], fill=FRAME, width=1)
    for x, y in ((inset + 3, inset + 3), (w - inset - 5, inset + 3),
                 (inset + 3, h - inset - 5), (w - inset - 5, h - inset - 5)):
        draw.rectangle((x, y, x + 1, y + 1), fill=DIM)


def prepare_logo() -> Image.Image:
    source = Image.open(SOURCE_LOGO).convert("RGBA")

    # The image editor preview can occasionally bake its transparency
    # checkerboard into an otherwise-opaque export. The mark is saturated
    # green/black, so near-neutral light pixels can be removed unambiguously.
    clean = Image.new("RGBA", source.size, (0, 0, 0, 0))
    src_px = source.load()
    clean_px = clean.load()
    for y in range(source.height):
        for x in range(source.width):
            r, g, b, a = src_px[x, y]
            neutral_light = min(r, g, b) > 175 and max(r, g, b) - min(r, g, b) < 22
            clean_px[x, y] = (r, g, b, 0 if a < 20 or neutral_light else a)

    alpha = clean.getchannel("A")
    bbox = alpha.point(lambda p: 255 if p > 12 else 0).getbbox()
    if bbox is None:
        raise RuntimeError("Generated logo has no visible pixels")
    logo = clean.crop(bbox)

    # Reduce AI-rendered softness to a small, hardware-friendly phosphor palette.
    px = logo.load()
    for y in range(logo.height):
        for x in range(logo.width):
            r, g, b, a = px[x, y]
            if a < 20:
                px[x, y] = (0, 0, 0, 0)
            elif g < 42:
                px[x, y] = DEEP
            elif g < 135:
                px[x, y] = FRAME
            elif g < 225:
                px[x, y] = GREEN
            else:
                px[x, y] = HOT
    return logo


def add_logo(image: Image.Image, logo: Image.Image, box: tuple[int, int, int, int]) -> None:
    x, y, width, height = box
    copy = logo.copy()
    copy.thumbnail((width, height), Image.Resampling.NEAREST)
    px = x + (width - copy.width) // 2
    py = y + (height - copy.height) // 2

    # A very small phosphor halo, kept behind the crisp nearest-neighbor mark.
    halo_alpha = copy.getchannel("A").filter(ImageFilter.GaussianBlur(radius=2.0))
    halo = Image.new("RGBA", copy.size, (18, 190, 70, 0))
    halo.putalpha(halo_alpha.point(lambda a: min(42, a // 5)))
    image.alpha_composite(halo, (px, py))
    image.alpha_composite(copy, (px, py))


def scanlines(image: Image.Image) -> None:
    overlay = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    for y in range(2, image.height, 4):
        draw.line((0, y, image.width - 1, y), fill=(0, 0, 0, 24))
    image.alpha_composite(overlay)


def draw_common(image: Image.Image) -> None:
    chamfered_frame(image, 5, 7)
    draw = ImageDraw.Draw(image)
    draw.line((14, 13, image.width - 15, 13), fill=FRAME, width=1)
    draw.rectangle((14, 10, 38, 15), fill=BG)
    draw.rectangle((18, 12, 20, 13), fill=GREEN)
    draw.rectangle((24, 12, 26, 13), fill=DIM)
    draw.rectangle((30, 12, 32, 13), fill=FRAME)


def pager_frame(logo: Image.Image, frame_index: int | None = None) -> Image.Image:
    image = Image.new("RGBA", (480, 222), BG)
    draw_common(image)
    centered_text(image, 24, "REMOTE CONSOLE", 2, DIM, tracking=1)
    add_logo(image, logo, (42, 51, 126, 126))
    ImageDraw.Draw(image).line((190, 55, 190, 176), fill=FRAME, width=1)
    centered_text_in(image, 207, 451, 61, "POCKET SSH", 3, GREEN)
    centered_text_in(image, 207, 451, 96, "SECURE SHELL TERMINAL", 1, DIM, tracking=1)
    ImageDraw.Draw(image).line((211, 118, 447, 118), fill=FRAME, width=1)
    cursor_on = frame_index is None or frame_index % 10 < 6
    centered_text_in(image, 207, 451, 138, "> LINK READY" + ("_" if cursor_on else " "), 2, HOT)
    centered_text(image, 195, "ENCRYPTED LINK", 1, DIM, tracking=1)
    scanlines(image)
    if frame_index is not None:
        animate_overlay(image, frame_index)
    return image.convert("RGB")


def deck_frame(logo: Image.Image, frame_index: int | None = None) -> Image.Image:
    image = Image.new("RGBA", (320, 240), BG)
    draw_common(image)
    centered_text(image, 25, "REMOTE CONSOLE", 1, DIM, tracking=1)
    add_logo(image, logo, (21, 57, 112, 112))
    ImageDraw.Draw(image).line((145, 58, 145, 173), fill=FRAME, width=1)
    centered_text_in(image, 153, 307, 67, "POCKET SSH", 2, GREEN)
    centered_text_in(image, 153, 307, 94, "SECURE SHELL", 1, DIM, tracking=1)
    centered_text_in(image, 153, 307, 105, "TERMINAL", 1, DIM, tracking=1)
    ImageDraw.Draw(image).line((158, 124, 302, 124), fill=FRAME, width=1)
    cursor_on = frame_index is None or frame_index % 10 < 6
    centered_text_in(image, 153, 307, 143, "> READY" + ("_" if cursor_on else " "), 2, HOT)
    centered_text(image, 212, "ENCRYPTED REMOTE CONSOLE", 1, DIM, tracking=1)
    scanlines(image)
    if frame_index is not None:
        animate_overlay(image, frame_index)
    return image.convert("RGB")


def animate_overlay(image: Image.Image, frame_index: int) -> None:
    """Add a quiet CRT sweep and low-amplitude breathing flicker."""
    count = 16
    sweep_y = round(-8 + frame_index * (image.height + 16) / (count - 1))
    sweep = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(sweep)
    draw.rectangle((6, sweep_y - 2, image.width - 7, sweep_y + 2), fill=(72, 255, 120, 8))
    draw.line((6, sweep_y, image.width - 7, sweep_y), fill=(182, 255, 188, 18), width=1)
    image.alpha_composite(sweep)

    # Keep flicker subtle enough that the splash never flashes.
    gain = 0.985 + 0.015 * math.sin((frame_index / count) * 2 * math.pi)
    rgb = ImageEnhance.Brightness(image.convert("RGB")).enhance(gain).convert("RGBA")
    image.paste(rgb)


def gif_palette() -> Image.Image:
    palette = Image.new("P", (1, 1))
    colors = [
        (0, 13, 5),
        (0, 27, 10),
        (7, 76, 31),
        (12, 139, 59),
        (31, 204, 77),
        (51, 255, 102),
        (112, 255, 140),
        (182, 255, 188),
    ]
    flat = [v for color in colors for v in color]
    palette.putpalette(flat + [0] * (768 - len(flat)))
    return palette


def save_gif(frames: list[Image.Image], path: Path) -> None:
    palette = gif_palette()
    indexed = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in frames]
    indexed[0].save(
        path,
        save_all=True,
        append_images=indexed[1:],
        duration=100,
        loop=0,
        disposal=2,
        optimize=True,
    )


def main() -> None:
    if OUT.exists():
        shutil.rmtree(OUT)
    (OUT / "assets" / "t-pager").mkdir(parents=True)
    (OUT / "assets" / "t-deck").mkdir(parents=True)
    (OUT / "source").mkdir(parents=True)

    logo = prepare_logo()
    logo.save(OUT / "source" / "pocket_ssh_logo_master.png", optimize=True)

    pager_static = pager_frame(logo)
    deck_static = deck_frame(logo)
    pager_static.save(OUT / "assets" / "t-pager" / "pocket_ssh_t-pager_480x222.png", optimize=True)
    deck_static.save(OUT / "assets" / "t-deck" / "pocket_ssh_t-deck_320x240.png", optimize=True)

    pager_frames = [pager_frame(logo, i) for i in range(16)]
    deck_frames = [deck_frame(logo, i) for i in range(16)]
    save_gif(pager_frames, OUT / "assets" / "t-pager" / "pocket_ssh_t-pager_480x222.gif")
    save_gif(deck_frames, OUT / "assets" / "t-deck" / "pocket_ssh_t-deck_320x240.gif")

    # QA contact sheet showing both native layouts without scaling either one.
    qa = Image.new("RGB", (830, 260), (18, 18, 18))
    qa.paste(pager_static, (10, 19))
    qa.paste(deck_static, (500, 10))
    qa.save(ROOT / "pocket_ssh_qa_contact_sheet.png", optimize=True)


if __name__ == "__main__":
    main()
