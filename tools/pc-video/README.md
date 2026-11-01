# PC GPU video and high-resolution still sequences

This separate desktop tool reads a T6 MOV or JPEG sequence, applies a film look
and anamorphic-inspired effects on CUDA, then writes a new HEVC/H.264 MP4 with
NVIDIA NVENC. It makes no camera/card/boot changes. The original media is read
only. CPU FFmpeg decoding, RGB transfers and RGB-to-YUV conversion surround
the GPU effects/encoding; this is not a zero-copy or entirely GPU pipeline.

## Which source gives the most resolution?

| Source | Captured dimensions/cadence | Use |
| --- | --- | --- |
| Normal T6 H.264 MOV | 1920×1080 up to 29.97 fps | Normal motion with audio |
| Normal T6 H.264 MOV | 1280×720 up to 59.94 fps | Faster motion / slow playback in an editor |
| Large JPEG still sequence | 5184×3456, up to about 3 stills/s | Maximum spatial detail; still/time-lapse/burst workflow |
| CR2 RAW sequence | Recorded image size 5184×3456, 14-bit RAW | Develop actual RAW separately, then provide JPEGs to this prototype |

These are [Canon specifications](https://sg.canon/en/support/6200330100).
The rated RAW burst is about six frames (five with simultaneous Large JPEG),
under Canon's test conditions. Neither a sustained 3 fps sequence nor a new
live 5K movie mode has been demonstrated here. JPEG burst performance depends
on settings/card. A sequence playback rate supplied to this program does not
measure the camera's acquisition rate.

Choose **HEVC for a full 5184×3456 sequence**. The RTX 3090/Ampere encoder
supports HEVC 8K dimensions and Main10 in
[NVIDIA's capability table](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/nvenc-application-note/index.html).
This prototype outputs **8-bit 4:2:0**; it does not develop the CR2's 14-bit RAW
sensor data. Its JPEG path cannot recover extra precision or clipped detail.
Changing codecs does not change the camera sensor's video readout. The tool
keeps source dimensions; no upscaling or frame interpolation is performed.

## Requirements and commands

Use Python with NumPy, Pillow and a **CUDA-enabled PyTorch** installation.
Use the [official PyTorch selector](https://pytorch.org/get-started/locally/)
for your OS/CUDA combination. FFmpeg and ffprobe must be on PATH; the FFmpeg
build must contain `hevc_nvenc` / `h264_nvenc`, and the NVIDIA driver must
support that build. See [NVIDIA's FFmpeg guide](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.0/ffmpeg-with-nvidia-gpu/index.html).
There is no CPU-effect or software-encoding fallback.

Run from the repository root, using new output names:

```powershell
python tools/pc-video/t6video.py --input "D:\Footage\MVI_0001.MOV" `
  -o "D:\Footage\MVI_0001-cinema.mp4" --anamorphic strong --tint blue

python tools/pc-video/t6video.py --sequence "D:\Footage\JPEG-sequence" --fps 3 `
  -o "D:\Footage\sequence-5k.mp4" --stock src/stocks/eterna.fs
```

The first audio stream is re-encoded to AAC, retaining its channels and sample
rate. Video keeps its exact rational source rate. Sequence JPEGs are sorted
lexically by filename, must have equal dimensions, and have no audio. Use
zero-padded names to express the intended order. EXIF rotation is not applied.
Use `--max-frames 60` for a short test, `--no-grain` to disable animated grain,
or `--codec h264` for a more broadly compatible 1080p result. Default NVENC
quality is `--cq 18` with preset p5. `--anamorphic off` disables the spatial
anamorphic stage while retaining the film look.

The CUDA look reads the shared `.fs` stock recipes, but its floating-point
curves, hue operations, halation, animated grain and spatial effects are a
**GPU interpretation**, not the bit-exact integer camera renderer. Streaks,
anisotropic bloom, vignette and chromatic offsets imitate some lens traits.
They do not reconstruct depth, optical oval bokeh or extra field of view.

## Timing, files and limits

Only zero-start, square-pixel, constant-frame-rate video is supported. Every
packet presentation timestamp is checked before processing; gaps, duplicates,
variable cadence, rotation metadata and offset audio are rejected. The bound
is two million packets/sequence frames. Source video/JPEGs are not rewritten.
The user must keep source files stable while a run is in progress.

The program writes a temporary MP4 in the output directory. It checks codec,
dimensions, frame count, rational rate, audio channels/sample rate and audio
timing (within one AAC frame), then fully decodes video/audio independently
before atomic new-file publication. Existing paths, including dangling links,
are refused. Publication requires a local filesystem that supports hard links
(e.g. NTFS); unsupported storage fails without an overwrite fallback. The
temporary directory is removed after success or failure.

Metadata subprocesses have a 60-second timeout, timestamp scanning and final
decode have 120-second limits. `--timeout 7200` is the default processing
deadline; a watchdog terminates only this run's FFmpeg children to release
blocked pipes. This cannot forcibly preempt a hung CUDA driver/kernel. Wall
time printed at completion includes checks and transfers, and is not camera
capture FPS or a GPU-only benchmark. High-resolution memory and speed depend
on the GPU and selected effects. Long exports may require higher limits in a
future tool revision; a timed-out run is not published.

Private camera clips and full local verification results stay under ignored
`out/`. Run `python tools/pc-video/test_pipeline.py` for the CPU-only stream,
timing and new-file contracts; `python tools/pc-video/test_effects.py` exercises
the CUDA effect stage on a machine with its dependencies.

## Verified local runs, 2026-11-01

RTX 3090 24 GB, NVIDIA driver 621.01, Python 3.13, PyTorch 2.11.0+cu126,
NumPy 2.2.1, Pillow 11.3.0 and Windows FFmpeg 7.0.2:

| Source/result | Frames and rate | Effect | Full run, including checks | Peak CUDA allocated bytes |
| --- | --- | --- | ---: | ---: |
| Owner's 1920×1080 MOV → HEVC/AAC | 349 at 30000/1001 | Portra / Strong / Blue | 11.965 s | 450,600,960 |
| Owner's 1280×720 MOV → HEVC/AAC | 800 at 60000/1001 | Eterna / Cinema / Amber | 14.632 s | 199,127,040 |
| 5184×3456 JPEG sequence → HEVC | 2 at playback rate 3 | Portra / Cinema / Blue | 1.764 s | 3,877,033,984 |

All output frames independently decoded with exact expected timestamp steps.
Both clip audio streams retained 48 kHz/two channels and passed the timing
checks. Source SHA-256 values were unchanged. The 5K test uses two repeats of
the full-size JPEG decoded from the public CC0 T6 sample; it proves processing
and encoding at that geometry, not a measured camera burst. A source/output
frame comparison was also inspected visually. Peak allocation is PyTorch's
allocator measurement, not total driver/NVENC/host memory. These short runs
are not sustained throughput or camera FPS benchmarks.
