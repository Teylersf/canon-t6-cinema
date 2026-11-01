#!/usr/bin/env python3
"""Rebuild the clearly labeled, entirely procedural effects reference.

Pillow draws the source scene; the project's real t6dev C pipeline applies
the same Portra recipe to three NEW temporary outputs. No camera or video
is involved. Requires Pillow and a built src/t6dev executable. GPL-2.0.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import random
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
W, H = 1152, 768


def font(size: int, bold: bool = False) -> ImageFont.ImageFont:
    name = "DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"
    for directory in ("/usr/share/fonts/truetype/dejavu", "C:/Windows/Fonts"):
        candidate = Path(directory) / name
        if candidate.is_file():
            return ImageFont.truetype(str(candidate), size)
    return ImageFont.load_default()


def source_scene() -> Image.Image:
    """Stylized night street: isolated highlights expose the flare kernels."""
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H
        d.line((0, y, W, y), fill=(int(5 + t * 12), int(10 + t * 11), int(19 + t * 17)))
    # Deliberately simple geometry keeps this visibly a synthetic test scene.
    d.rectangle((0, 115, 275, 554), fill=(16, 24, 34))
    d.polygon(((900, 115), (1151, 77), (1151, 559), (900, 562)), fill=(20, 26, 33))
    d.rectangle((315, 219, 480, 533), fill=(19, 31, 44))
    d.rectangle((595, 189, 747, 535), fill=(13, 25, 35))
    d.polygon(((0, 547), (W, 547), (W, H), (0, H)), fill=(20, 31, 43))
    d.polygon(((478, 549), (672, 549), (955, H), (224, H)), fill=(27, 39, 49))
    rng = random.Random(1300)
    for left, right, top in ((12, 267, 136), (910, 1140, 137), (328, 468, 237), (609, 734, 206)):
        for y in range(top, 491, 48):
            for x in range(left, right - 18, 39):
                on = rng.random() < 0.37
                color = (128, 105, 67) if on else (26, 44, 57)
                d.rectangle((x, y, x + 16, y + 22), fill=color)
    for y in range(586, H, 38):
        d.line((0, y, W, y), fill=(30, 45, 58), width=1)
    for i in range(70):
        y = rng.randrange(565, H)
        x = int(570 + rng.gauss(0, 70) * ((y - 510) / 120))
        half = rng.randrange(6, 29)
        d.line((x - half, y, x + half, y), fill=(55, 84, 103), width=2)
    # A neon storefront and warm window establish skin-tone-free color cues.
    d.rectangle((56, 360, 252, 485), fill=(9, 23, 33), outline=(35, 108, 127), width=4)
    d.text((82, 381), "STUDIO", font=font(31, True), fill=(112, 235, 242))
    d.text((109, 428), "OPEN 24", font=font(18), fill=(233, 145, 83))
    d.rectangle((953, 372, 1117, 513), fill=(125, 80, 44))
    d.rectangle((967, 386, 1103, 499), fill=(214, 162, 101))
    # The hot cores are intentionally compact and unclipped JPEG whites.
    lights = ((295, 337, 12), (842, 286, 13), (546, 403, 8), (670, 456, 5))
    glow = Image.new("RGB", im.size)
    gd = ImageDraw.Draw(glow)
    for x, y, radius in lights:
        gd.ellipse((x - radius, y - radius, x + radius, y + radius), fill=(110, 128, 145))
    glow = glow.filter(ImageFilter.GaussianBlur(8))
    # Add only a small source glow; long streaks come from the C effect.
    from PIL import ImageChops
    im = ImageChops.add(im, glow)
    d = ImageDraw.Draw(im)
    for x, y, radius in lights:
        d.line((x, y + radius, x, 561), fill=(47, 60, 69), width=4)
        d.ellipse((x - radius, y - radius, x + radius, y + radius), fill=(255, 255, 255))
        d.ellipse((x - radius - 3, y - radius - 3, x + radius + 3, y + radius + 3),
                  outline=(139, 169, 188), width=2)
    # Low foreground silhouette and red rear lights give another bright hue.
    d.rounded_rectangle((739, 563, 1005, 663), radius=18, fill=(10, 17, 25))
    d.polygon(((783, 566), (813, 522), (934, 522), (972, 567)), fill=(14, 24, 35))
    d.rectangle((754, 597, 785, 608), fill=(255, 38, 27))
    d.rectangle((960, 597, 991, 608), fill=(255, 38, 27))
    return im


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "src/t6dev")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/images/anamorphic-demo.jpg")
    args = parser.parse_args()
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error("build src/t6dev first")
    panels = []
    choices = (("Effects off", "off", "blue"),
               ("Cinema / blue", "cinema", "blue"),
               ("Strong / amber", "strong", "amber"))
    with tempfile.TemporaryDirectory(prefix="t6-synthetic-reference-") as temporary:
        directory = Path(temporary)
        source = directory / "synthetic-night.jpg"
        source_scene().save(source, quality=98, subsampling=0)
        env = os.environ.copy()
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        for label, preset, tint in choices:
            output = directory / f"{preset}-{tint}.jpg"
            subprocess.run([str(binary), str(source), "-o", str(output),
                            "--stock", str(ROOT / "src/stocks/portra.fs"),
                            "--anamorphic", preset, "--tint", tint,
                            "--scale", "1", "--quality", "88"],
                           cwd=ROOT / "src", env=env, check=True)
            with Image.open(output) as image:
                panels.append((label, image.convert("RGB").resize((384, 256), Image.Resampling.LANCZOS)))
    canvas = Image.new("RGB", (1200, 396), (13, 19, 28))
    d = ImageDraw.Draw(canvas)
    d.text((15, 12), "ANAMORPHIC-INSPIRED STILL EFFECTS", font=font(20, True), fill=(236, 242, 247))
    d.text((15, 40), "SYNTHETIC REFERENCE — NOT CAMERA OUTPUT", font=font(13, True), fill=(234, 184, 107))
    for i, (label, panel) in enumerate(panels):
        x = 12 + i * 396
        d.text((x + 2, 75), label, font=font(15, True), fill=(220, 230, 240))
        canvas.paste(panel, (x, 100))
    d.text((15, 371), "Same procedural scene + Portra color in all panels. Desktop still-image reference; no camera or video recording.",
           font=font(12), fill=(153, 173, 192))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(args.output, quality=91, subsampling=0, optimize=True)
    print(f"Saved {args.output} (1200×396; synthetic reference only)")


if __name__ == "__main__":
    main()
