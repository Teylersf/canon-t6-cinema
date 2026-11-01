#!/usr/bin/env python3
"""CFR T6 video/JPEG sequence -> CUDA look -> NVIDIA HEVC/H.264. GPL-2.0.

CPU FFmpeg decoding and RGB transfers surround the CUDA effect stage. NVENC
does the final compression. No camera firmware or original media is modified.
"""
from __future__ import annotations

import argparse
from fractions import Fraction
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]


def command_json(args):
    result = subprocess.run(args, capture_output=True, check=True, text=True, timeout=60)
    return json.loads(result.stdout)


def probe(path, ffprobe):
    data = command_json([ffprobe, "-v", "error", "-show_streams", "-of", "json", str(path)])
    video = next((s for s in data["streams"] if s["codec_type"] == "video"), None)
    if video is None:
        raise ValueError("Input has no video stream")
    return data, video


def check_cfr(path, stream, rate, ffprobe, work):
    """Check every packet PTS, including presentation reordering from B-frames.

    Reject gaps, duplicates, VFR, and nonzero starts instead of silently losing
    their timing in an RGB pipe. A two-million-frame bound caps the PTS list.
    """
    ticks = Fraction(stream["time_base"])
    pts = []
    with (work / "timestamps.log").open("wb") as log:
        proc = subprocess.Popen([
            ffprobe, "-v", "error", "-select_streams", str(stream["index"]),
            "-show_packets", "-show_entries", "packet=pts", "-of", "csv=p=0", str(path)
        ], stdout=subprocess.PIPE, stderr=log, text=True)
        watchdog = threading.Timer(120, stop, args=(proc,))
        watchdog.daemon = True
        watchdog.start()
        try:
            for line in proc.stdout:
                # Some demuxers add side-data fields after the requested PTS.
                value = line.strip().split(",", 1)[0]
                if not value:
                    continue
                pts.append(int(value))
                if len(pts) > 2_000_000:
                    raise ValueError("Input exceeds the two-million-frame timing-check limit")
            if proc.wait(timeout=30):
                raise ValueError("FFprobe timestamp check failed")
        finally:
            watchdog.cancel()
            proc.stdout.close()
            stop(proc)
    if not pts:
        raise ValueError("Input has no timestamped video packets")
    pts.sort()
    for index, actual in enumerate(pts):
        expected = Fraction(index, 1) / rate / ticks
        if abs(Fraction(actual) - expected) > Fraction(1, 2):
            raise ValueError("Only zero-start constant-frame-rate video is supported; input has a gap, duplicate or varying timestamps")
    return len(pts)


def stop(proc):
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=10)


def read_frame(stream, size):
    result = bytearray()
    while len(result) < size:
        part = stream.read(size - len(result))
        if not part:
            if result:
                raise ValueError("Decoder returned a truncated RGB frame")
            return None
        result.extend(part)
    return result


def write_all(stream, data):
    view = memoryview(data).cast("B")
    while view:
        count = stream.write(view)
        if not count:
            raise OSError("Encoder stopped accepting frame data")
        view = view[count:]


def dimensions(width, height):
    if width % 2 or height % 2 or not (144 <= width <= 8192 and 128 <= height <= 8192):
        raise ValueError("NVENC 4:2:0 requires even dimensions within 144..8192 by 128..8192")


def publish(source, output):
    # Same-volume hard-link publication is atomic and refuses a racing target.
    # Do not use replace()/rename() which can overwrite on some platforms.
    os.link(source, output)


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--input", type=Path, help="Zero-start CFR video, e.g. the T6's original MOV")
    source.add_argument("--sequence", type=Path, help="Directory of same-size .JPG/.JPEG frames in filename order")
    parser.add_argument("--fps", type=Fraction, help="Required playback rate for a JPEG sequence; not a capture-rate claim")
    parser.add_argument("-o", "--output", type=Path, required=True, help="New .mp4 path on a filesystem supporting hard links")
    parser.add_argument("--stock", type=Path, default=ROOT / "src/stocks/portra.fs")
    parser.add_argument("--anamorphic", choices=("off", "subtle", "cinema", "strong"), default="cinema")
    parser.add_argument("--tint", choices=("blue", "amber"), default="blue")
    parser.add_argument("--no-grain", action="store_true")
    parser.add_argument("--codec", choices=("hevc", "h264"), default="hevc")
    parser.add_argument("--cq", type=int, default=18, help="NVENC constant-quality target 0..51 (default 18)")
    parser.add_argument("--max-frames", type=int, help="Optional short test; trims audio to the selected video duration")
    parser.add_argument("--timeout", type=int, default=7200, help="Processing deadline in seconds; terminates owned FFmpeg children (default 7200)")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    args = parser.parse_args()
    if not 0 <= args.cq <= 51 or (args.max_frames is not None and args.max_frames <= 0) or args.timeout <= 0:
        parser.error("CQ must be 0..51; max-frames and timeout must be positive")
    if args.input and args.fps is not None:
        parser.error("Video keeps its original rate; --fps is only for --sequence")
    if args.sequence and (args.fps is None or not 0 < args.fps <= 240):
        parser.error("JPEG sequences require --fps greater than zero and at most 240")
    return args


def run(args):
    import numpy as np
    import torch
    from PIL import Image
    from effects import GpuLook

    if not torch.cuda.is_available():
        raise ValueError("A working CUDA-enabled PyTorch installation is required; no CPU fallback")
    if not shutil.which(args.ffmpeg) or not shutil.which(args.ffprobe):
        raise ValueError("FFmpeg and ffprobe must be installed or supplied explicitly")
    output = args.output.absolute()
    if output.suffix.lower() != ".mp4":
        raise ValueError("Use a new .mp4 output path")
    if os.path.lexists(output):
        raise FileExistsError(f"Refusing existing output: {output}")
    if not output.parent.is_dir():
        raise ValueError("Output parent directory must already exist")
    look = GpuLook(args.stock, args.anamorphic, args.tint, not args.no_grain)
    decoder = encoder = None
    started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix=".t6video-", dir=output.parent) as folder:
        work = Path(folder)
        files = []
        audio = False
        source_audio = None
        source = None
        if args.input:
            source = args.input.resolve(strict=True)
            if not source.is_file():
                raise ValueError("Video input must be a regular file")
            metadata, video = probe(source, args.ffprobe)
            rate = Fraction(video["avg_frame_rate"])
            if not 0 < rate <= 240:
                raise ValueError("Invalid or unsupported input frame rate")
            width, height = int(video["width"]), int(video["height"])
            # RGB frame piping does not carry rotation or non-square pixels.
            if video.get("sample_aspect_ratio", "1:1") not in ("1:1", "N/A"):
                raise ValueError("Only square-pixel sources are supported")
            if any(int(item.get("rotation", 0)) != 0 for item in video.get("side_data_list", [])):
                raise ValueError("Rotated video needs explicit preparation before this processor")
            if int(video.get("tags", {}).get("rotate", 0)) != 0:
                raise ValueError("Rotated video needs explicit preparation before this processor")
            expected = check_cfr(source, video, rate, args.ffprobe, work)
            source_audio = next((s for s in metadata["streams"] if s["codec_type"] == "audio"), None)
            audio = source_audio is not None
            for stream in metadata["streams"]:
                if stream["codec_type"] == "audio" and abs(float(stream.get("start_time", 0))) > 0.001:
                    raise ValueError("Nonzero audio start is unsupported; align the source first")
            if audio and any(key not in source_audio for key in ("duration", "channels", "sample_rate")):
                raise ValueError("Audio requires explicit duration, channel and sample-rate metadata")
        else:
            directory = args.sequence.resolve(strict=True)
            if not directory.is_dir():
                raise ValueError("Sequence input must be a directory")
            files = sorted(p for p in directory.iterdir() if p.suffix.lower() in (".jpg", ".jpeg"))
            if not files or len(files) > 2_000_000 or any(not p.is_file() for p in files):
                raise ValueError("Sequence must contain 1..2000000 regular JPEG files")
            with Image.open(files[0]) as image:
                width, height = image.size
            rate, expected = args.fps, len(files)
        dimensions(width, height)
        if args.codec == "h264" and (width > 4096 or height > 4096):
            raise ValueError("Use HEVC for dimensions above 4096 with this Ampere-oriented tool")
        count = min(expected, args.max_frames or expected)
        duration = Fraction(count, 1) / rate
        encoded = work / "processed.mp4"
        enc_args = [args.ffmpeg, "-hide_banner", "-v", "error", "-xerror", "-n",
                    "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", f"{width}x{height}",
                    "-framerate", str(rate), "-i", "pipe:0"]
        if audio:
            enc_args += ["-i", str(source)]
        enc_args += ["-map", "0:v:0"]
        if audio:
            enc_args += ["-map", "1:a:0", "-c:a", "aac", "-b:a", "256k"]
        enc_args += ["-c:v", f"{args.codec}_nvenc", "-preset", "p5", "-rc", "vbr", "-cq", str(args.cq),
                     "-b:v", "0", "-pix_fmt", "yuv420p", "-color_range", "tv", "-colorspace", "bt709",
                     "-color_primaries", "bt709", "-color_trc", "bt709",
                     "-vf", "scale=in_range=full:out_range=tv:out_color_matrix=bt709,setsar=1",
                     "-t", f"{float(duration):.9f}", "-movflags", "+faststart"]
        if args.codec == "hevc":
            enc_args += ["-tag:v", "hvc1"]
        enc_args += [str(encoded)]
        with (work / "encode.log").open("wb") as enc_log, (work / "decode.log").open("wb") as dec_log:
            watchdog = None
            expired = threading.Event()
            try:
                encoder = subprocess.Popen(enc_args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=enc_log)
                if source:
                    decoder = subprocess.Popen([
                        args.ffmpeg, "-hide_banner", "-v", "error", "-xerror", "-err_detect", "explode",
                        "-noautorotate", "-i", str(source), "-map", f"0:{video['index']}",
                        "-an", "-sn", "-dn", "-frames:v", str(count), "-fps_mode", "passthrough",
                        "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"
                    ], stdout=subprocess.PIPE, stderr=dec_log)
                def expire():
                    expired.set()
                    stop(encoder)
                    stop(decoder)
                watchdog = threading.Timer(args.timeout, expire)
                watchdog.daemon = True
                watchdog.start()
                torch.cuda.reset_peak_memory_stats()
                with torch.inference_mode():
                    for index in range(count):
                        if source:
                            raw = read_frame(decoder.stdout, width * height * 3)
                            if raw is None:
                                raise ValueError(f"Decoder ended after {index} of {count} frames")
                            rgb = np.frombuffer(raw, dtype=np.uint8).reshape(height, width, 3)
                        else:
                            with Image.open(files[index]) as image:
                                if image.size != (width, height):
                                    raise ValueError(f"Sequence dimensions changed at {files[index].name}")
                                rgb = np.array(image.convert("RGB"), dtype=np.uint8)
                        frame = torch.from_numpy(rgb).to(device="cuda", dtype=torch.float32).permute(2, 0, 1) / 255.0
                        result = look(frame, index)
                        if expired.is_set():
                            raise TimeoutError("Processing deadline reached")
                        data = (result.clamp(0, 1) * 255).round().to(torch.uint8).permute(1, 2, 0).contiguous().cpu().numpy()
                        write_all(encoder.stdin, data)
                        if index == 0 or (index + 1) % 120 == 0:
                            print(f"CUDA effects + NVENC: {index + 1}/{count} frames", file=sys.stderr, flush=True)
                encoder.stdin.close()
                if decoder:
                    decoder.stdout.close()
                    if decoder.wait(timeout=60):
                        raise ValueError("FFmpeg decoding failed")
                if encoder.wait(timeout=120):
                    raise ValueError("FFmpeg NVENC encoding failed")
                if expired.is_set():
                    raise TimeoutError("Processing deadline reached")
            except BaseException:
                stop(decoder)
                stop(encoder)
                for name in ("decode.log", "encode.log"):
                    path = work / name
                    text = path.read_text(errors="replace")[-8000:]
                    if text:
                        print(text, file=sys.stderr)
                raise
            finally:
                if watchdog:
                    watchdog.cancel()
                stop(decoder)
                stop(encoder)
        saved_metadata, saved = probe(encoded, args.ffprobe)
        if (int(saved["width"]), int(saved["height"])) != (width, height) or int(saved.get("nb_frames", -1)) != count:
            raise ValueError("Encoded output dimensions/frame count do not match the processed source")
        if Fraction(saved["avg_frame_rate"]) != rate or saved["codec_name"] != ("hevc" if args.codec == "hevc" else "h264"):
            raise ValueError("Encoded output codec/frame rate differs from the requested result")
        saved_audio = next((s for s in saved_metadata["streams"] if s["codec_type"] == "audio"), None)
        if audio:
            if saved_audio is None or saved_audio["codec_name"] != "aac":
                raise ValueError("Encoded output is missing its AAC audio stream")
            if any(saved_audio[key] != source_audio[key] for key in ("channels", "sample_rate")):
                raise ValueError("Encoded audio channels/sample rate changed")
            tolerance = 1024 / int(saved_audio["sample_rate"]) + 0.001
            audio_duration = min(float(source_audio["duration"]), float(duration))
            if abs(float(saved_audio["duration"]) - audio_duration) > tolerance or abs(float(saved_audio.get("start_time", 0))) > tolerance:
                raise ValueError("Encoded audio timing exceeds one AAC frame of tolerance")
        elif saved_audio is not None:
            raise ValueError("Unexpected audio in silent sequence output")
        # Full independent decoder pass, including audio, before publication.
        subprocess.run([args.ffmpeg, "-v", "error", "-xerror", "-err_detect", "explode", "-i", str(encoded),
                        "-map", "0:v:0", "-map", "0:a:0?", "-f", "null", "-"], check=True, timeout=120)
        publish(encoded, output)
        return {"output": str(output), "width": width, "height": height, "frames": count,
                "fps": str(rate), "source_audio": audio, "audio_encoding": "AAC" if audio else None,
                "codec": saved["codec_name"], "gpu": torch.cuda.get_device_name(0),
                "cuda_peak_allocated_bytes": torch.cuda.max_memory_allocated(),
                "wall_seconds_including_checks": round(time.perf_counter() - started, 3),
                "bytes": output.stat().st_size}


def main():
    try:
        print(json.dumps(run(parse_args()), indent=2))
        return 0
    except (ValueError, OSError, subprocess.SubprocessError, RuntimeError, ImportError) as error:
        print(f"t6video: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
