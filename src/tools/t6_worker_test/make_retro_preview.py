#!/usr/bin/env python3
"""Rasterize commands from the actual shared C dashboard renderer.

Host DejaVu mono glyphs approximate ML's bitmap fonts; geometry and all text
come from film_retro_draw.c. The artifact is not a camera screenshot.
"""
import json
from pathlib import Path
import subprocess
import tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[3]
OVERLAY = ROOT / "integrations/magiclantern/1300D.110/src"
PALETTE = ((10, 10, 7), (235, 161, 48), (105, 86, 52), (233, 204, 132))
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"


def render(commands):
    im = Image.new("RGB", (commands["width"], commands["height"]))
    d = ImageDraw.Draw(im)
    fonts = [ImageFont.truetype(FONT_PATH, size) for size in (12, 20, 32)]
    for op in commands["ops"]:
        if op[0] == "fill":
            _, x, y, w, h, color = op
            d.rectangle((x, y, x + w - 1, y + h - 1), fill=PALETTE[color])
        else:
            _, x, y, size, color, text = op
            d.text((x, y), text, font=fonts[size], fill=PALETTE[color], anchor="lt")
    return im


def main():
    with tempfile.TemporaryDirectory(prefix="t6-retro-preview-") as temp:
        binary = str(Path(temp) / "retro-preview")
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I" + str(OVERLAY),
                        str(Path(__file__).with_name("retro_preview.c")),
                        str(OVERLAY / "film_retro_draw.c"), "-o", binary], check=True)
        images = [render(json.loads(subprocess.check_output([binary, str(i)]))) for i in range(3)]
    # All three native layouts, scaled only for a compact repository preview.
    result = Image.new("RGB", (1168, 824), (19, 20, 18))
    d = ImageDraw.Draw(result)
    f = ImageFont.truetype(FONT_PATH, 16)
    d.text((18, 14), "T6 CINEMA / SHARED C RENDERER PREVIEW", font=f, fill=PALETTE[1])
    d.text((18, 39), "Software layout reference. Hardware untested; not a camera screenshot.", font=ImageFont.truetype(FONT_PATH, 12), fill=PALETTE[3])
    for image, xy in zip(images, ((16, 66), (592, 66), (16, 447))):
        result.paste(image.resize((560, 373), Image.Resampling.LANCZOS), xy)
    d.text((607, 491), "LOOK DECK + SHOOT DESK + GUIDE", font=f, fill=PALETTE[1])
    note = ("D-pad up (ISO) navigates in the console;\n"
            "native ISO applies outside it.\n\n"
            "MENU closes it. Shutter, AV/erase,\n"
            "playback, LV and zoom leave it for\n"
            "their native camera controls.\n\n"
            "No raw unknown-button remapping.\n"
            "No change to movie encoding or FPS.")
    d.multiline_text((607, 535), note, font=ImageFont.truetype(FONT_PATH, 13), fill=PALETTE[3], spacing=8)
    target = ROOT / "docs/images/retro-dashboard.png"
    target.parent.mkdir(parents=True, exist_ok=True)
    result.save(target, optimize=True)
    print(target)


if __name__ == "__main__":
    main()
