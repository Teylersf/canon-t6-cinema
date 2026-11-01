#!/usr/bin/env python3
"""Build an experimental EOS Rebel T6 / 1300D.110 overlay.

Requires Linux (or WSL), GNU make, git, gcc and arm-none-eabi-gcc.
Reads a clean, exactly pinned ML checkout; builds an isolated archive copy.
It never installs, flashes, enables boot flags, or writes to a camera/card.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
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
FONT_FILES = ["term12.rbf", "term20.rbf", "arghlf22.rbf", "argnor23.rbf",
              "argnor28.rbf", "argnor32.rbf"]
STOCK_PACKAGE_NAMES = {
    "bleachbypass.fs": "bleach.fs",
    "classicchrome.fs": "classchr.fs",
    "classicneg.fs": "classneg.fs",
    "nostalgicneg.fs": "nostneg.fs",
}
YUV_INPUTS = ["yuv_anamorphic.c", "yuv_anamorphic.h"]
HEVC_INPUTS = ["hevc_intra.c", "hevc_intra.h"]
ML_RAM_START = 0xC80000
ML_RAM_END = 0xD00000


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


def validate_ram_image(path):
    """Keep the complete ARM load segment, including BSS, in the T6 reserve.

    This is a static-image bound, not a measurement of heap or task-stack use.
    The exact pinned platform reserves [0xC80000, 0xD00000) for ML.
    """
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        fail("expected a little-endian ELF32 ARM image")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    _, kind, machine, version, entry, phoff, _, _, ehsize, phsize, phnum, *_ = header
    if (kind, machine, version, entry, ehsize, phsize) != (2, 40, 1, ML_RAM_START, 52, 32):
        fail("unexpected T6 ARM ELF header or entry point")
    if not 1 <= phnum <= 16 or phoff < 52 or phoff + phnum * phsize > len(data):
        fail("invalid T6 ELF program-header table")
    loads = []
    for number in range(phnum):
        segment = struct.unpack_from("<8I", data, phoff + number * phsize)
        ptype, offset, address, physical, files, memory, _, _ = segment
        if ptype != 1:
            continue
        if (not memory or files > memory or offset + files > len(data) or
                address != physical or address < ML_RAM_START or address + memory > ML_RAM_END):
            fail("T6 static load/BSS exceeds the reserved RAM range")
        loads.append((address, address + memory))
    if len(loads) != 1 or loads[0][0] != ML_RAM_START:
        fail("unexpected T6 ELF load-segment layout")
    end = loads[0][1]
    return {"start": ML_RAM_START, "end_exclusive": ML_RAM_END,
            "static_load_bytes": end - ML_RAM_START,
            "remaining_reserved_bytes": ML_RAM_END - end,
            "includes_bss": True, "excludes_heap_and_task_stacks": True}


def sanitize_build_identity(stage):
    """Replace two exactly pinned recipes in the isolated archive only.

    Upstream otherwise embeds the local account and host in AUTOEXEC.BIN.
    Keep timestamps and the private stage path, but use a public build identity.
    """
    path = stage / "platform" / "Makefile"
    text = path.read_text()
    identity = "`whoami`@`hostname`"
    markers = [
        "\t@echo 'Built on : '`date -u \"+%Y-%m-%d %H:%M:%S\"`' by '`whoami`@`hostname` >> $@.tmp\n",
        "\t@echo 'const char build_user[] = \"'`whoami`@`hostname`'\";'  >> $(BUILD_DIR)/version.tmp\n",
    ]
    if text.count(identity) != 2 or any(text.count(marker) != 1 for marker in markers):
        fail("build identity recipe mismatch in pinned platform/Makefile")
    for marker in markers:
        text = text.replace(marker, marker.replace(identity, "T6-Cinema-source-build"))
    path.write_text(text)


def patch_retro_ui(stage):
    """Checked source patches in an isolated, exactly pinned ML archive only.

    Startup/powersave/shutdown handling runs before the new gate. All existing
    feature handlers retain their order. Native handoff skips only the AV menu
    shortcut and ML-menu actions; it never skips the core event/safety chain.
    """
    def replace(path, marker, replacement):
        text = path.read_text()
        if text.count(marker) != 1:
            fail("retro UI marker mismatch in " + str(path.relative_to(stage)))
        path.write_text(text.replace(marker, replacement))

    common = stage / "src/gui-common.c"
    replace(common, "#include <lvinfo.h>\n",
            '#include <lvinfo.h>\n#include "film_retro_native.h"\n')
    marker = '    /* convert Q replacement events into BGMT_Q */\n'
    replace(common, marker,
            '    /* T6 Cinema: startup, shutdown and powersave checks above remain native. */\n'
            '    int t6_retro_route = film_retro_event_gate(event);\n'
            '    if (t6_retro_route == 0) return 0;\n\n' + marker)
    marker = ('    #ifdef CONFIG_MENU_WITH_AV\n'
              '    if (handle_av_short_for_menu(event) == 0) return 0;\n'
              '    #endif\n')
    replace(common, marker,
            '    #ifdef CONFIG_MENU_WITH_AV\n'
            '    if (t6_retro_route != 2 && handle_av_short_for_menu(event) == 0) return 0;\n'
            '    #endif\n')
    menu = stage / "src/menu.c"
    replace(menu, '#include "menu.h"\n',
            '#include "menu.h"\n#include "film_retro_native.h"\n')
    marker = ('    // on some cameras, scroll events may arrive grouped; we can\'t handle it, so split into individual events\n')
    replace(menu, marker,
            '    /* Handoff only bypasses menu actions, after native display checks. */\n'
            '    if (film_retro_menu_passthrough(event)) return 1;\n\n' + marker)

    # The pinned menu semaphore carries untyped toggles. A half-press key and
    # the redraw task can both request closure before either wake is consumed,
    # reopening the menu. In this T6 archive only, publish an absolute intent
    # first; the semaphore then only wakes the consumer. A delayed wake cannot
    # turn a completed close into an open. Keep all native close producers.
    replace(menu, '            // close ML menu\n            give_semaphore(gui_sem);\n',
            '            // close ML menu\n            t6_menu_request(T6_MENU_CLOSE);\n')
    replace(menu, '        redraw_flood_stop = 1;\n        give_semaphore(gui_sem);\n',
            '        redraw_flood_stop = 1;\n        t6_menu_request(T6_MENU_CLOSE);\n')
    replace(menu, 'gui_stop_menu( )\n{\n    if (gui_menu_shown())\n        give_semaphore(gui_sem);\n}',
            'gui_stop_menu( )\n{\n    t6_menu_request(T6_MENU_CLOSE);\n}')
    replace(menu, 'gui_open_menu( )\n{\n    if (!gui_menu_shown())\n        give_semaphore(gui_sem);\n}',
            'gui_open_menu( )\n{\n    t6_menu_request(T6_MENU_OPEN);\n}')
    text = menu.read_text()
    if text.count('give_semaphore( gui_sem );') != 4 or 'give_semaphore(gui_sem);' in text:
        fail('T6 menu semaphore producer mismatch in pinned src/menu.c')
    menu.write_text(text.replace('give_semaphore( gui_sem );',
                                 't6_menu_request(T6_MENU_TOGGLE);'))
    replace(menu, '#include "film_retro_native.h"\n',
            '#include "film_retro_native.h"\n#include "arm-mcr.h"\n')
    marker = 'static int menu_shown = false;\n'
    replace(menu, marker, marker + '''
/* T6 menu intent helper begin: also compiled by the host scheduling test. */
enum { T6_MENU_CLOSE, T6_MENU_OPEN, T6_MENU_TOGGLE };
static volatile int t6_menu_desired_state;
static void t6_menu_request(int intent)
{
    /* cli/sei have no compiler memory clobber in the pinned ARM helpers. */
    __asm__ __volatile__("" ::: "memory");
    uint32_t irq = cli();
    __asm__ __volatile__("" ::: "memory");
    int desired = intent == T6_MENU_TOGGLE
        ? !t6_menu_desired_state : intent == T6_MENU_OPEN;
    int changed = desired != t6_menu_desired_state;
    t6_menu_desired_state = desired;
    __asm__ __volatile__("" ::: "memory");
    sei(irq);
    __asm__ __volatile__("" ::: "memory");
    /* Never invoke DryOS while interrupts are disabled. Another wake may
     * consume this intent before this post; applying it again is harmless. */
    if (changed)
        give_semaphore(gui_sem);
}
/* T6 menu intent helper end. */
''')
    marker = '''            /* menu toggle request */
            if (menu_shown)
            {
                menu_close();
            }
            else
            {
                menu_open();
                initial_mode = shooting_mode;
            }
'''
    replace(menu, marker, '''            /* T6 menu intent consumer begin. */
            int desired = t6_menu_desired_state;
            if (desired && !menu_shown)
            {
                menu_open();
                initial_mode = shooting_mode;
            }
            else if (!desired && menu_shown)
            {
                menu_close();
            }
            /* T6 menu intent consumer end. */
''')
    # This alternative task is disabled upstream, but must not retain toggle
    # semantics if it is selected while using the same request helper.
    marker = '''        if( !menu_shown )
        {
            //~ menu_shown = true;
            menu_open();
        }
        else
        {
            menu_close();
            //~ menu_shown = false;
        }
'''
    replace(menu, marker, '''        int desired = t6_menu_desired_state;
        if (desired && !menu_shown)
            menu_open();
        else if (!desired && menu_shown)
            menu_close();
''')


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
    for name in YUV_INPUTS:
        if not (ROOT / "src" / "experimental" / "yuv_anamorphic" / name).is_file():
            fail("missing bounded YUV benchmark input: " + name)
    for name in HEVC_INPUTS:
        if not (ROOT / "src" / "experimental" / "hevc_intra" / name).is_file():
            fail("missing bounded HEVC proof input: " + name)
    if not (ROOT / "src" / "experimental" / "hevc_intra" / "THIRD_PARTY.md").is_file():
        fail("missing HEVC third-party binary distribution notice")
    for name in FONT_FILES:
        if not (source / "data" / "fonts" / name).is_file():
            fail("missing pinned ML bitmap font: " + name)


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
    # The YUV backend is linked only for the owned synthetic CPU benchmark.
    # Add it after the engine hash pass so its true source paths are recorded.
    for name in YUV_INPUTS:
        original = ROOT / "src" / "experimental" / "yuv_anamorphic" / name
        destination = stage / "soup_engine" / name
        if destination.exists():
            fail("unexpected duplicate YUV engine input: " + name)
        shutil.copy2(original, destination)
        hashes[original.relative_to(ROOT).as_posix()] = sha256(destination)
    # Only the small freestanding encoder core enters the camera build.
    # Host input adapters and external codec experiments remain separate.
    for name in HEVC_INPUTS:
        original = ROOT / "src" / "experimental" / "hevc_intra" / name
        destination = stage / "soup_engine" / name
        if destination.exists():
            fail("unexpected duplicate HEVC engine input: " + name)
        shutil.copy2(original, destination)
        hashes[original.relative_to(ROOT).as_posix()] = sha256(destination)
    for name in FONT_FILES:
        hashes["data/fonts/" + name] = sha256(stage / "data" / "fonts" / name)
    shutil.copy2(Path(__file__), metadata / "build-t6.py")
    shutil.copy2(ROOT / "LICENSE", metadata / "LICENSE")
    hevc_notice = ROOT / "src" / "experimental" / "hevc_intra" / "THIRD_PARTY.md"
    shutil.copy2(hevc_notice, metadata / "HEVCLIC.TXT")
    hashes[hevc_notice.relative_to(ROOT).as_posix()] = sha256(metadata / "HEVCLIC.TXT")
    hashes["src/tools/build-t6.py"] = sha256(metadata / "build-t6.py")
    hashes["LICENSE"] = sha256(metadata / "LICENSE")
    (metadata / "source-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")
    makefile = stage / "platform" / PLATFORM / "Makefile"
    text = makefile.read_text()
    marker = "include ../Makefile"
    if text.count(marker) != 1:
        fail("unexpected pinned platform Makefile layout")
    makefile.write_text(text.replace(marker, "include t6-film.mk\n\n" + marker))
    patch_retro_ui(stage)
    sanitize_build_identity(stage)


def package_stocks(source, destination):
    """Use stable 8.3 names for Canon FIO; keep source names and bodies intact."""
    index = source / "INDEX.TXT"
    if not index.is_file() or index.is_symlink():
        fail("missing regular stock INDEX.TXT")
    entries = index.read_text(encoding="ascii").splitlines()
    if not entries or len({name.casefold() for name in entries}) != len(entries):
        fail("stock index must contain existing, unique recipe names")
    mapping = {}
    packaged_names = set()
    for name in entries:
        if not re.fullmatch(r"[A-Za-z0-9_]{1,64}\.fs", name):
            fail("invalid stock index filename: " + repr(name))
        recipe = source / name
        if not recipe.is_file() or recipe.is_symlink():
            fail("indexed stock recipe is missing or not a regular file: " + name)
        packaged = STOCK_PACKAGE_NAMES.get(name, name)
        if not re.fullmatch(r"[A-Za-z0-9_]{1,8}\.[A-Za-z0-9_]{1,3}", packaged):
            fail("packaged stock filename is not 8.3: " + packaged)
        if packaged.casefold() in packaged_names:
            fail("packaged stock filenames collide: " + packaged)
        packaged_names.add(packaged.casefold())
        mapping[name] = packaged
    available = {path.name.casefold() for path in source.iterdir()
                 if path.is_file() and path.suffix.casefold() == ".fs"}
    if available != {name.casefold() for name in entries}:
        fail("stock index must list every source recipe exactly once")
    # Validate the whole plan before creating or copying any packaged files.
    destination.mkdir()
    for original, packaged in mapping.items():
        shutil.copy2(source / original, destination / packaged)
    (destination / "INDEX.TXT").write_text(
        "".join(mapping[name] + "\n" for name in entries), encoding="ascii")
    return mapping


def package(stage, destination, log, toolchain):
    platform = stage / "platform" / PLATFORM / "build"
    ram_image = validate_ram_image(platform / "magiclantern")
    destination.mkdir(parents=True, exist_ok=False)
    shutil.copy2(platform / "autoexec.bin", destination / "AUTOEXEC.BIN")
    modules = destination / "ML" / "modules"
    modules.mkdir(parents=True)
    shutil.copy2(platform / "t6_110.sym", modules / "t6_110.sym")
    stocks = destination / "ML" / "FILM"
    stock_file_map = package_stocks(stage / "t6_stocks", stocks)
    # Without these assets ML falls back to Canon's much larger font, breaking
    # both the native menus and the retro dashboard's measured layout.
    fonts = destination / "ML" / "FONTS"
    fonts.mkdir()
    for name in FONT_FILES:
        source_font = stage / "data" / "fonts" / name
        if source_font.read_bytes()[:8] != b"\xe0\x0e\xf0\x0d\x03\x00\x00\x00":
            fail("unexpected pinned RBF font header: " + name)
        shutil.copy2(source_font, fonts / name)
    shutil.copy2(stage / "t6_inputs" / "README.md", destination / "README.md")
    shutil.copy2(stage / "t6_inputs" / "LICENSE", destination / "LICENSE")
    shutil.copy2(stage / "t6_inputs" / "HEVCLIC.TXT", destination / "HEVCLIC.TXT")
    shutil.copy2(log, destination / "build.log")
    manifest = {
        "camera": "Canon EOS Rebel T6 / EOS 1300D / Kiss X80",
        "canon_firmware": "1.1.0", "platform": PLATFORM,
        "status": "EXPERIMENTAL - LIMITED PHYSICAL STILL VALIDATION - NO INSTALLER",
        "magic_lantern_commit": ML_PIN,
        "soup_engine_base_commit": SOUP_BASE,
        "built_utc": datetime.now(timezone.utc).isoformat(),
        "toolchain": toolchain,
        "build_identity": "T6-Cinema-source-build",
        "build_identity_source_patch": "platform/Makefile (two checked identity recipes)",
        "stage": str(stage),
        "source_hashes": json.loads((stage / "t6_inputs" / "source-hashes.json").read_text()),
        "artifact_hashes": {},
        "static_ram_image": ram_image,
        "stock_file_map": stock_file_map,
        "limits": {"batch": 4, "preview_bytes": 12 * 1024 * 1024,
                   "output_bytes": 8 * 1024 * 1024, "deadline_seconds": 300},
        "source_cr2_access": "read only; no preview replacement or deletion",
        "temporary_creation": "FIO_OpenFile T6-specific 0x8501 create-new; no truncate or destructive fallback",
        "native_fio_validation": "exclusive-create and rename collision semantics hardware unverified",
        "retro_ui": "native custom-menu renderer; checked pinned source gates; hardware untested",
        "retro_ui_source_patches": ["src/gui-common.c", "src/menu.c"],
        "video_cpu_benchmark": "owned synthetic Os/uncached, Os/cached and O2/cached comparison with output checks; no encoder hook or recording-FPS result",
        "synthetic_cpu_report_version": 2,
        "hevc_proof": "manual owned synthetic HEVC I_PCM; no live sensor capture or compression-performance claim",
        "hevc_encoder_workspace_bytes": 5632,
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
    print("Built experimental overlay:", args.output.resolve())
    print("No camera, card, boot flag or firmware was modified.")


if __name__ == "__main__":
    main()
