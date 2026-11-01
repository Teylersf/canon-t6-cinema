#!/usr/bin/env python3
"""End-to-end image, source-preservation and output-failure checks. GPL-2.0."""
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
BIN = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "src/t6dev").resolve()
checks = []


def check(condition, name):
    if not condition:
        raise AssertionError(name)
    checks.append(name)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(source, output, *args, success=True):
    p = subprocess.run([str(BIN), str(source), "-o", str(output), *map(str, args)],
                       capture_output=True, text=True)
    check((p.returncode == 0) == success, f"exit: {' '.join(map(str, args))}: {p.stderr}")
    if success:
        with Image.open(output) as image:
            image.load()
            check(image.format == "JPEG", "independent Pillow decoder accepts output")
    return p


def scene(w=641, h=433):
    y, x = np.mgrid[:h, :w]
    a = np.zeros((h, w, 3), dtype=np.uint8)
    a[:, :, 0] = 10 + (x * 45 // w)
    a[:, :, 1] = 12 + (y * 40 // h)
    a[:, :, 2] = 24 + (x * 28 // w)
    image = Image.fromarray(a)
    d = ImageDraw.Draw(image)
    for cx, cy, color in [(190, 160, (255, 255, 250)), (450, 260, (255, 245, 220))]:
        d.ellipse((cx-4, cy-4, cx+4, cy+4), fill=color)
    d.rectangle((260, 100, 340, 330), fill=(165, 90, 45))
    return image


def container(jpeg, width, height):
    # A synthetic CR2 header exercises the embedded-preview path only.
    # Real camera RAW parsing is covered separately with the CC0 T6 sample.
    offset = 16 + 2 + 5 * 12 + 4
    tags = [(256, width), (257, height), (259, 6), (273, offset), (279, len(jpeg))]
    header = b"II*\0" + struct.pack("<I", 16) + b"CR\x02\0" + struct.pack("<I", 0)
    return header + struct.pack("<H", 5) + b"".join(
        struct.pack("<HHII", key, 4, 1, value) for key, value in tags
    ) + b"\0\0\0\0" + jpeg


with tempfile.TemporaryDirectory(prefix="t6-cli-") as temp:
    folder = Path(temp)
    src = folder / "night scene.jpg"
    scene().save(src, quality=96, subsampling=1)
    original = digest(src)
    off = folder / "off.jpg"
    run(src, off, "--anamorphic", "off", "--scale", 1)
    check(Image.open(off).size == (640, 432), "MCU-aligned dimensions are reported and valid")
    base = np.array(Image.open(off)).astype(np.int16)
    expected = np.array(Image.open(src))[:432, :640].astype(np.int16)
    check(np.abs(base - expected).mean() < 8, "neutral grade preserves color within JPEG loss")
    strong = folder / "strong.jpg"
    run(src, strong, "--anamorphic", "strong", "--scale", 1)
    styled = np.array(Image.open(strong)).astype(np.int16)
    check(np.abs(styled - base).mean() > 0.2, "strong preset has a visible effect")
    warm = folder / "amber.jpg"
    run(src, warm, "--anamorphic", "strong", "--tint", "amber", "--scale", 1)
    check(digest(warm) != digest(strong), "amber and blue produce distinct output")
    crop = folder / "scope.jpg"
    run(src, crop, "--scale", 1, "--aspect", "2.39")
    check(Image.open(crop).size == (640, 264), "2.39 framing center-crops without stretching")
    squeeze = folder / "optical.jpg"
    run(src, squeeze, "--scale", 1, "--desqueeze", "1.33")
    check(Image.open(squeeze).size == (848, 432), "explicit optical desqueeze dimensions")
    synthetic = folder / "fixture.CR2"
    synthetic.write_bytes(container(src.read_bytes(), 641, 433))
    shash = digest(synthetic)
    run(synthetic, folder / "cr2.jpg", "--stock", ROOT / "src/stocks/soup800.fs")
    check(digest(synthetic) == shash, "CR2 source bytes unchanged")
    before = digest(off)
    run(src, off, success=False)
    check(digest(off) == before, "existing output bytes unchanged")
    run(src, src, success=False)
    check(digest(src) == original, "same-path input cannot be overwritten")
    link = folder / "hardlink.jpg"
    link.hardlink_to(src)
    run(src, link, success=False)
    check(digest(src) == original, "hardlink cannot overwrite input")
    sym = folder / "symlink.jpg"
    sym.symlink_to(src)
    run(src, sym, success=False)
    check(digest(src) == original, "symlink cannot overwrite input")
    for args in [("--scale", "0"), ("--scale", "3"), ("--quality", "0"),
                 ("--desqueeze", "NaN"), ("--aspect", "0"), ("--anamorphic", "invalid")]:
        bad = folder / "invalid.jpg"
        run(src, bad, *args, success=False)
        check(not bad.exists(), "invalid arguments create no partial output")
    prog = folder / "progressive.jpg"
    scene().save(prog, progressive=True)
    run(prog, folder / "progressive-out.jpg", success=False)
    for k in [0, 1, 3, 16, 64, 200]:
        truncated = folder / "truncated.jpg"
        truncated.write_bytes(src.read_bytes()[:k])
        bad = folder / "truncated-out.jpg"
        run(truncated, bad, success=False)
        check(not bad.exists(), "truncated headers leave no output")
    check(digest(src) == original, "original JPEG unchanged after entire suite")

    sample = ROOT / "src/samples/T6_CC0.CR2"
    if sample.exists():
        check(digest(sample) == "e064327405b3a736b1cc89d499bc5a049cfb2800bcb06a5072e6d0a82cd296d7",
              "real T6 CC0 fixture checksum")
        p = run(sample, folder / "real-t6.jpg", "--stock", ROOT / "src/stocks/soup800.fs")
        check("Canon EOS 1300D" in p.stdout, "real T6 model metadata parsed")
        check(Image.open(folder / "real-t6.jpg").size == (2592, 1728), "real T6 embedded JPEG pipeline")
        check(digest(sample) == "e064327405b3a736b1cc89d499bc5a049cfb2800bcb06a5072e6d0a82cd296d7",
              "real T6 source preserved")

print(json.dumps({"suite": "t6-cli", "passed": len(checks)}, sort_keys=True))
