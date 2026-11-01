#!/usr/bin/env python3
"""Build a hardware-untested EOS Rebel T6 / 1300D.110 experiment.

Requires Linux (or WSL), GNU make, git, gcc and arm-none-eabi-gcc.
Reads a clean, exactly pinned ML checkout; builds an isolated archive copy.
It never installs, flashes, enables boot flags, or writes to a camera/card.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
OVERLAY = ROOT / "integrations" / "magiclantern" / "1300D.110"
ML_PIN = "142472e95054d73640f50789ac0fd66325499d82"
SOUP_BASE = "d393e4df4ec3ae5d98038b5dc5c2607b05236076"
PLATFORM = "1300D.110"
ENGINE_FILES = ["se", "cr2", "stock", "look", "jpeg_dec", "jpeg_enc",
                "wb", "color", "grade", "lut", "anamorphic"]


def fail(message):
    raise SystemExit("build-t6: " + message)


def output(command, cwd=None):
    return subprocess.check_output(command, cwd=cwd, text=True).strip()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_source(source):
    if output(["git", "rev-parse", "HEAD"], source) != ML_PIN:
        fail("ML checkout must be exactly " + ML_PIN)
    # A Windows checkout read through WSL has CRLF working files. Treat those
    # exactly as Windows Git does, without changing the source's configuration.
    if output(["git", "-c", "core.autocrlf=true", "status", "--porcelain",
               "--untracked-files=no"], source):
        fail("ML checkout has tracked changes; use a clean pinned checkout")
    makefile = (source / "platform" / PLATFORM / "Makefile").read_text()
    if "MODEL = 1300D" not in makefile or "FW_VERSION = 110" not in makefile:
        fail("pinned platform is not EOS 1300D firmware 1.1.0")
    for name in ENGINE_FILES:
        if not (ROOT / "src" / "engine" / (name + ".c")).is_file():
            fail("missing engine source: " + name)
    if not (OVERLAY / "src" / "film_develop.c").is_file():
        fail("missing 1300D overlay")


def extract_source(source, stage):
    # git archive ignores dirty/untracked files and keeps executable modes.
    with subprocess.Popen(["git", "archive", "--format=tar", ML_PIN],
                          cwd=source, stdout=subprocess.PIPE) as process:
        with tarfile.open(fileobj=process.stdout, mode="r|") as archive:
            for member in archive:
                target = (stage / member.name).resolve()
                if not target.is_relative_to(stage.resolve()) or member.issym() or member.islnk():
                    fail("unexpected path/link in pinned source archive")
                archive.extract(member, stage, filter="data")
        if process.wait():
            fail("git archive failed")
    shutil.copytree(ROOT / "src" / "engine", stage / "soup_engine")
    shutil.copytree(ROOT / "src" / "stocks", stage / "t6_stocks")
    metadata = stage / "t6_inputs"
    metadata.mkdir()
    hashes = {}
    for path in OVERLAY.rglob("*"):
        if path.is_file():
            relative = path.relative_to(OVERLAY)
            destination = (stage if relative.parts[0] in ("src", "platform") else metadata) / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
            hashes[path.relative_to(ROOT).as_posix()] = sha256(destination)
    for name, original in [("soup_engine", "src/engine"), ("t6_stocks", "src/stocks")]:
        for path in (stage / name).rglob("*"):
            if path.is_file():
                hashes[original + "/" + path.relative_to(stage / name).as_posix()] = sha256(path)
    shutil.copy2(Path(__file__), metadata / "build-t6.py")
    shutil.copy2(ROOT / "LICENSE", metadata / "LICENSE")
    hashes["src/tools/build-t6.py"] = sha256(metadata / "build-t6.py")
    hashes["LICENSE"] = sha256(metadata / "LICENSE")
    (metadata / "source-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")
    makefile = stage / "platform" / PLATFORM / "Makefile"
    text = makefile.read_text()
    marker = "include ../Makefile"
    if text.count(marker) != 1:
        fail("unexpected pinned platform Makefile layout")
    makefile.write_text(text.replace(marker, "include t6-film.mk\n\n" + marker))


def package(stage, destination, log, toolchain):
    platform = stage / "platform" / PLATFORM / "build"
    destination.mkdir(parents=True, exist_ok=False)
    shutil.copy2(platform / "autoexec.bin", destination / "AUTOEXEC.BIN")
    modules = destination / "ML" / "modules"
    modules.mkdir(parents=True)
    shutil.copy2(platform / "t6_110.sym", modules / "t6_110.sym")
    stocks = destination / "ML" / "FILM"
    stocks.mkdir()
    for path in (stage / "t6_stocks").iterdir():
        if path.suffix == ".fs" or path.name == "INDEX.TXT":
            shutil.copy2(path, stocks / path.name)
    shutil.copy2(stage / "t6_inputs" / "README.md", destination / "README.md")
    shutil.copy2(stage / "t6_inputs" / "LICENSE", destination / "LICENSE")
    shutil.copy2(log, destination / "build.log")
    manifest = {
        "camera": "Canon EOS Rebel T6 / EOS 1300D / Kiss X80",
        "canon_firmware": "1.1.0", "platform": PLATFORM,
        "status": "EXPERIMENTAL - HARDWARE UNTESTED - NO INSTALLER",
        "magic_lantern_commit": ML_PIN,
        "soup_engine_base_commit": SOUP_BASE,
        "built_utc": datetime.now(timezone.utc).isoformat(),
        "toolchain": toolchain,
        "stage": str(stage),
        "source_hashes": json.loads((stage / "t6_inputs" / "source-hashes.json").read_text()),
        "artifact_hashes": {},
        "limits": {"batch": 4, "preview_bytes": 12 * 1024 * 1024,
                   "output_bytes": 8 * 1024 * 1024, "deadline_seconds": 300},
        "source_cr2_access": "read only; no preview replacement or deletion",
        "temporary_creation": "FIO_OpenFile O_WRONLY|O_CREAT|O_EXCL; no destructive fallback",
        "native_fio_validation": "exclusive-create and rename collision semantics hardware unverified",
    }
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            manifest["artifact_hashes"][path.relative_to(destination).as_posix()] = sha256(path)
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    lines = []
    for path in sorted(destination.rglob("*")):
        if path.is_file():
            lines.append(sha256(path) + "  " + path.relative_to(destination).as_posix())
    (destination / "SHA256SUMS").write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ml-source", required=True, type=Path,
                        help="read-only source checkout at exactly the pinned commit")
    parser.add_argument("--output", type=Path,
                        default=ROOT / "out" / "t6-experimental",
                        help="new local output directory; must not already exist")
    parser.add_argument("--build-parent", type=Path,
                        help="optional build parent with no spaces (default: system temp)")
    parser.add_argument("--prepare-only", action="store_true",
                        help="validate and stage sources without compiling")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if os.name == "nt":
        fail("run this script under Linux/WSL; no camera/card writing is supported")
    if args.output.exists():
        fail("output already exists; choose a new directory to avoid overwriting")
    if not 1 <= args.jobs <= 16:
        fail("--jobs must be 1..16")
    for command in ["git", "make", "gcc", "arm-none-eabi-gcc", "arm-none-eabi-objcopy"]:
        if not shutil.which(command):
            fail("missing build prerequisite " + command)
    source = args.ml_source.resolve()
    check_source(source)
    stage = Path(tempfile.mkdtemp(prefix="t6-cinema-", dir=args.build_parent))
    if any(character.isspace() for character in str(stage)):
        fail("build path contains whitespace; use --build-parent /tmp")
    extract_source(source, stage)
    print("Isolated source stage:", stage, flush=True)
    if args.prepare_only:
        return
    log = stage / "build-t6.log"
    toolchain = output(["arm-none-eabi-gcc", "--version"]).splitlines()[0]
    command = ["make", "-j" + str(args.jobs), "FATAL_WARNINGS=n",
               "VERSION=T6-Cinema-experimental.1300D.110",
               "build/autoexec.bin", "build/t6_110.sym"]
    with log.open("w") as file:
        result = subprocess.run(command, cwd=stage / "platform" / PLATFORM,
                                stdout=file, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text()[-16000:], file=sys.stderr)
        fail("compile failed; retained stage and log at " + str(log))
    package(stage, args.output.resolve(), log, toolchain)
    print("Built hardware-untested experiment:", args.output.resolve())
    print("No camera, card, boot flag or firmware was modified.")


if __name__ == "__main__":
    main()
