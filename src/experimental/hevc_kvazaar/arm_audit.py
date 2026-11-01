#!/usr/bin/env python3
"""Compile-only ARM946/newlib audit. No native link, runtime shim or execution."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from run import PIN, REPO


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--semaphore-declarations", action="store_true",
                        help="compile-only incomplete POSIX semaphore declarations; NO implementation")
    args = parser.parse_args()
    source = args.source.resolve()
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != PIN:
        raise RuntimeError("source pin mismatch")
    output = Path(tempfile.mkdtemp(prefix="hevc-kvazaar-arm-", dir=REPO / "out"))
    print("OUTPUT=" + str(output), flush=True)
    files = sorted((source / "src").glob("*.c"))
    files = [p for p in files if p.name not in {"encmain.c", "cli.c", "yuv_io.c"}]
    files += sorted((source / "src/strategies").rglob("*.c"))
    files.append(source / "src/extras/libmd5.c")
    flags = ["-std=c99", "-Os", "-mcpu=arm946e-s", "-marm", "-mfloat-abi=soft",
             "-ffreestanding", "-fno-builtin", "-ffunction-sections", "-fdata-sections",
             "-fstack-usage", "-DCMAKE_BUILD", "-D_POSIX_C_SOURCE=200809L",
             "-D_POSIX_THREADS=200809L", "-D_POSIX_READER_WRITER_LOCKS=200809L",
             "-I" + str(source / "src"), "-I" + str(source / "src/extras"),
             "-I" + str(source / "src/strategies")]
    if args.semaphore_declarations:
        declarations = output / "declarations-only"
        declarations.mkdir()
        # The library only needs declarations through an unused inline helper;
        # no sem_t instances are created in these CLI-excluded source files.
        # An INCOMPLETE type deliberately supplies no pretend target ABI.
        (declarations / "semaphore.h").write_text(
            "#ifndef KVZ_COMPILE_ONLY_SEMAPHORE_H\n#define KVZ_COMPILE_ONLY_SEMAPHORE_H\n"
            "typedef struct kvz_unimplemented_posix_semaphore sem_t;\n"
            "int sem_init(sem_t *, int, unsigned);\nint sem_wait(sem_t *);\n"
            "int sem_post(sem_t *);\nint sem_destroy(sem_t *);\n#endif\n")
        flags.extend(["-I" + str(declarations), "-D_DEFAULT_SOURCE"])
    # Feature macros expose newlib's POSIX declarations only. They DO NOT
    # supply a pthread/clock/OS implementation and are not a camera port.
    objects, failures = [], []
    for path in files:
        name = "_".join(path.relative_to(source).parts)
        obj = output / (name + ".o")
        result = subprocess.run(["arm-none-eabi-gcc", *flags, "-c", str(path), "-o", str(obj)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / (name + ".log")).write_text(result.stdout)
        if result.returncode:
            failures.append({"source": str(path.relative_to(source)), "error": result.stdout})
        else:
            objects.append(obj)
    report = {"pin": PIN, "compile_only": True, "runtime_available": False,
              "incomplete_semaphore_declarations_only": args.semaphore_declarations,
              "camera_fps_measured": False, "compiler_flags": flags,
              "sources": len(files), "objects": len(objects), "failures": failures}
    core = output / "generic-core-relocatable.o"
    subprocess.run(["arm-none-eabi-ld", "-r", "-o", str(core), *map(str, objects)], check=True)
    for key, command in [("size", ["arm-none-eabi-size", str(core)]),
                         ("sections", ["arm-none-eabi-size", "-A", str(core)]),
                         ("undefined", ["arm-none-eabi-nm", "-u", str(core)])]:
        report[key] = subprocess.check_output(command, text=True)
    stack = []
    for path in output.glob("*.su"):
        for line in path.read_text().splitlines():
            parts = line.split("\t")
            if len(parts) == 3 and parts[1].isdigit():
                stack.append({"function": parts[0], "bytes": int(parts[1]), "kind": parts[2]})
    report["largest_individual_stack_frames_not_call_chain"] = sorted(stack, key=lambda x:x["bytes"], reverse=True)[:12]
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"sources": report["sources"], "objects": report["objects"],
                      "failed_sources": [f["source"] for f in failures],
                      "size": report["size"], "undefined": report["undefined"],
                      "largest_stack_frames": report["largest_individual_stack_frames_not_call_chain"][:3]}), flush=True)
    print("REPORT=" + str(output / "report.json"), flush=True)


if __name__ == "__main__":
    main()
