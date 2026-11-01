#!/usr/bin/env python3
"""Reproducible host-only experiment; every output uses a fresh ignored folder."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile

PIN = "6040962bed5cc68c5ad01234c38c08b8b2822068"
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
WRAPS = ["malloc", "calloc", "realloc", "free", "strdup", "pthread_create"]


def command(args, log, timeout=120):
    with log.open("w") as stream:
        process = subprocess.Popen([str(a) for a in args], stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            stream.write("\nHOST PROCESS TIMEOUT; no camera timing inference\n")
            return 124


def host(source, output, full, sanitize):
    build = output / "build"
    flags = "-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" if sanitize else "-O2"
    cmake = ["cmake", "-S", source, "-B", build, "-DBUILD_SHARED_LIBS=OFF",
             "-DBUILD_TESTS=OFF", "-DBUILD_KVAZAAR_BINARY=OFF", "-DUSE_CRYPTO=OFF",
             "-DGIT_SUBMODULE=OFF", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_C_FLAGS={flags}"]
    if command(cmake, output / "configure.log"):
        raise RuntimeError("CMake configure failed; see configure.log")
    if command(["cmake", "--build", build, "-j", "4"], output / "build.log"):
        raise RuntimeError("host library build failed; see build.log")
    executable = output / "harness"
    args = ["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", *flags.split(),
            "-I" + str(source / "src"), HERE / "harness.c", build / "libkvazaar.a",
            *["-Wl,--wrap=" + name for name in WRAPS], "-lm", "-pthread", "-o", executable]
    if command(args, output / "harness-build.log"):
        raise RuntimeError("harness build failed; see harness-build.log")
    cases = [("small", 64, 64, 3, 64 << 20, 1 << 20, 4, 0),
             ("sd", 320, 192, 2, 64 << 20, 4 << 20, 4, 0),
             ("budget", 64, 64, 1, 1, 1 << 20, 4, 70),
             ("cancel_before", 64, 64, 3, 64 << 20, 1 << 20, 0, 3),
             ("cancel_between", 64, 64, 3, 64 << 20, 1 << 20, 1, 3),
             ("output_limit", 64, 64, 1, 64 << 20, 1, 4, 4),
             ("odd_width", 65, 64, 1, 64 << 20, 1 << 20, 4, 2)]
    if full:
        cases.extend([("4k", 3840, 2160, 1, 512 << 20, 16 << 20, 4, 0),
                      ("8k", 7680, 4320, 1, 1024 << 20, 64 << 20, 4, 0)])
    report = {"pin": PIN, "native_integrated": False, "camera_fps_measured": False,
              "allocation_metric": "wrapped requested live bytes; excludes allocator metadata/libc internals/static/stack",
              "cases": []}
    for name, width, height, frames, memory, limit, cancel, expected in cases:
        bitstream = output / (name + ".hevc")
        log = output / (name + ".log")
        result = command([executable, bitstream, width, height, frames, memory, limit, cancel], log, 60)
        text = log.read_text()
        details = {"case": name, "exit_code": result, "expected": expected}
        for line in text.splitlines():
            if line.startswith("{"):
                details.update(json.loads(line))
        if result != expected:
            report["cases"].append(details)
            (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
            raise RuntimeError(f"{name}: exit{result}, expected{expected}; see {log}")
        if bitstream.exists() and bitstream.stat().st_size > limit:
            raise RuntimeError("output bound violated")
        if not result:
            probe = subprocess.check_output(["ffprobe", "-v", "error", "-count_frames",
                "-show_entries", "stream=codec_name,profile,width,height,pix_fmt,nb_read_frames",
                "-of", "json", str(bitstream)], text=True)
            details["ffprobe"] = json.loads(probe)
            stream = details["ffprobe"]["streams"][0]
            if (stream["codec_name"], stream["width"], stream["height"], int(stream["nb_read_frames"])) != ("hevc", width, height, frames):
                raise RuntimeError("decoder metadata mismatch")
            if command(["ffmpeg", "-v", "error", "-xerror", "-i", bitstream,
                        "-f", "null", "-"], output / (name + "-decode.log"), 60):
                raise RuntimeError("full HEVC decode failed")
            details["full_decode_passed"] = True
            if details.get("requested_live_bytes") or details.get("created_worker_threads"):
                raise RuntimeError("tracked leak or worker-thread creation")
        report["cases"].append(details)
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(details), flush=True)
    # Exclusive host creation must preserve even the harness's own existing file.
    before = (output / "small.hevc").read_bytes()
    collision = command([executable, output / "small.hevc", 64, 64, 1, 64 << 20,
                         1 << 20, 4], output / "existing-output.log")
    if collision != 5 or (output / "small.hevc").read_bytes() != before:
        raise RuntimeError("existing output was not preserved")
    report["existing_output_preserved"] = True
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--full", action="store_true", help="also encode one synthetic4K/8K frame on host")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != PIN:
        raise RuntimeError("source pin mismatch")
    output_parent = REPO / "out"
    output_parent.mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="hevc-kvazaar-", dir=output_parent))
    print("OUTPUT=" + str(output), flush=True)
    host(source, output, args.full, args.sanitize)
    print("PASS=" + str(output / "report.json"), flush=True)


if __name__ == "__main__":
    main()
