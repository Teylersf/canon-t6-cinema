# Custom bounded HEVC I_PCM experiment

This new C99 encoder writes real Annex-B HEVC/H.265 intra pictures. It has
independently generated and decoded **3840×2160 and 7680×4320 synthetic frames**
with FFmpeg, recovering every visible Y, Cb and Cr byte exactly. The code is not
a wrapper around an installed HEVC encoder and does not relabel another codec.

It uses HEVC's I_PCM mode: sample bytes are carried directly instead of motion
search, transforms and entropy-coded residuals. This deliberately trades
compression efficiency for a small bounded implementation. Two 4K synthetic
frames used 25,100,251 bytes; one 8K frame used 49,831,282 bytes. These are large
files, not compressed-video bitrate targets or camera-speed measurements.

## What the implementation contains

- VPS/SPS/PPS and independent IDR pictures, 8-bit planar 4:2:0.
- Fixed 32×32 coding units, I_PCM samples and PCM-aware CABAC restart.
- Row-major block callbacks and a bounded buffered byte-sink callback.
- Annex-B start codes and emulation-prevention escaping, including zero-heavy
  images; SPS conformance-window cropping of padded coding-unit edges.
- Even visible dimensions up to 8192, with coded area no greater than the
  declared level 6.2 limit of 35,651,584 luma samples. One to sixteen frames.
- A caller-owned 5,632-byte workspace, a small stack state, and **no heap**,
  threads, OS calls, file operations, camera addresses or capture hooks in the
  encoder core. ARM946E-S compilation has no undefined runtime symbols.

The headers declare Main profile, high tier, level 6.2. No timing/frame
rate, audio or MP4 container is written. Optional color-only VUI can explicitly
signal BT.601 matrix6, limited range and centered4:2:0; primaries and transfer
remain unspecified2. The default `hi_encode` output is unchanged and omits
color VUI, so a player may otherwise guess left chroma/unknown matrix.
Decoder interoperability is tested;
formal profile/level conformance certification is not claimed.

## Source and sink contracts

See [hevc_intra.h](hevc_intra.h). `hi_encode` snapshots the configuration and
callback descriptors. It requests one 1,536-byte CTU at a time in strict
frame-major, row-major order: 1,024 Y bytes followed by 256 Cb and 256 Cr bytes.
The caller must pad samples beyond visible dimensions without reading beyond
its input. A streaming source can retain just one CTU row rather than a frame.

The sink must accept an entire provided block and return exactly 1. The byte
budget covers actual escaped output, including headers and start codes. Source
failure, sink failure, cancellation or budget exhaustion stops the operation;
partial output is not a completed stream. Cancellation is checked before every
CTU and frame, so a callback already running cannot be preempted. Calls and
callbacks must be bounded, serialized and non-reentrant, with exclusive access
to workspace and stats. `read_ctu` may fill only its designated CTU span; it
must not mutate other workspace bytes or stats. No callback may reenter.

`bytes_committed` counts complete byte blocks accepted by the sink. CTU counts
refer to serialized complete CTUs, which may still be buffered if a later error
occurs. Only `HI_OK` with the requested frame count establishes completion.

## Build and verify

Requires a C99 compiler; decode tests also need Python3, NumPy, FFmpeg and
ffprobe. The surrounding project's Ubuntu build prerequisites provide them
except FFmpeg, which can be installed from the Ubuntu package repository.

```sh
make test asan arm
make decode
python3 verify_decode.py --large --keep /path/to/new-test-directory
```

The host-only synthetic CLI refuses an existing output path:

```sh
make OUT=/tmp/hevc-intra-synthetic
/tmp/hevc-intra-synthetic 3840 2160 2 /path/to/new-synthetic.hevc
```

The separate host-only raw420 adapter consumes an existing exact-length planar
YUV420 file, reads it through a bounded CTU-row cache and also refuses existing
output. It uses 184,320 row bytes at width3840, plus the encoder workspace:

```sh
make raw RAW_OUT=/tmp/hevc-intra-raw420
/tmp/hevc-intra-raw420 3840 2160 1 input.yuv /path/to/new-real-source.hevc
# For the fullsize frontend's explicitly BT.601 limited centered420 output:
/tmp/hevc-intra-raw420 3840 2160 1 input.yuv /path/to/new-color.hevc --bt601-limited-centered
```

The adapter accepts no Y4M header; use its raw420 payload as input. This source
adapter is not compiled into the freestanding core. Cropping and any image
effect must be performed by the source; the codec preserves supplied samples.

The decoder test independently checks codec, visible dimensions, frame count,
full decode without errors and exact recovered sample hashes. It covers 2×2,
32×32, cropped 34×66, multiple 64×64 frames, zero-heavy escaping, plus optional
4K and 8K. Core tests exercise bad dimensions and declared-level bounds,
metadata/workspace overlap, pointer-span wrap, source/sink failure, output
budgets, cancellation, snapshotting and escaping, under ASan/UBSan.

## Camera work remains separate

Synthetic 4K/8K HEVC generation establishes a software bitstream path. It does
not establish live sensor readout, an owned Canon frame buffer, real-time
encoding, SD throughput or in-camera playback. An 8K synthetic frame does not
prove an 8K optical capture mode on the T6. Any native proof must start with
owned synthetic pixels and new-file publication, then independently validate
its saved output. No camera integration is included in this folder.

## Provenance

Original experiment code is GPL-2.0-only. The arithmetic simplification uses
the normative HEVC MPS interval and PCM restart rules: fixed I-slice QP26 and
`part_mode` initialization 184 make every context-coded bin MPS1; the engine
restarts after PCM, and the context progresses to state 62. No general CABAC
encoder, prediction search or transform code is included.

Primary references:

- [HM 16.18 PCM finish/alignment](https://hevc.hhi.fraunhofer.de/HM-doc/_t_enc_bin_coder_c_a_b_a_c_8cpp_source.html)
- [FFmpeg n6.1.1 HEVC CABAC decoding](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavcodec/hevc_cabac.c)
- [FFmpeg n6.1.1 parameter-set decoding](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavcodec/hevc_ps.c)
- [FFmpeg n6.1.1 level limits](https://github.com/FFmpeg/FFmpeg/blob/n6.1.1/libavcodec/h265_profile_level.c)
- [Kvazaar's normative LPS range table at immutable commit 6040962](https://github.com/ultravideo/kvazaar/blob/6040962bed5cc68c5ad01234c38c08b8b2822068/src/cabac.c)

The single selected LPS-table column is retained with Kvazaar's notice in
[THIRD_PARTY.md](THIRD_PARTY.md). No FFmpeg decoder implementation is copied
or linked into the encoder; FFmpeg is the external validation tool.
