# Isolated Kvazaar HEVC host experiment

This folder contains custom host harness/audit code. It is excluded from the
camera build and accesses no camera, card, capture buffer or ROM. The HEVC
encoder itself is upstream Kvazaar, not newly written here. The separate
minimal codec experiment is independent.

## Provenance and API

Pinned upstream release: **v2.3.2**, commit
`6040962bed5cc68c5ad01234c38c08b8b2822068`, published September 17, 2025.
The [release](https://github.com/ultravideo/kvazaar/releases/tag/v2.3.2),
[source](https://github.com/ultravideo/kvazaar/tree/6040962bed5cc68c5ad01234c38c08b8b2822068),
[BSD3-Clause license](https://github.com/ultravideo/kvazaar/blob/6040962bed5cc68c5ad01234c38c08b8b2822068/LICENSE)
and [API](https://github.com/ultravideo/kvazaar/blob/6040962bed5cc68c5ad01234c38c08b8b2822068/src/kvazaar.h)
are primary sources. A subsequent upstream master commit was observed dated
September 30, 2026; the experiment deliberately uses the stable immutable pin.
Upstream source/build outputs remain in ignored `vendor/` and `out/` folders.
The custom files here are GPL-2.0-only; no upstream codec source is vendored.

The harness uses `kvz_api_get(8)`, configuration allocation/initialization,
`encoder_open`, `picture_alloc`, `encoder_encode`, chunk/picture release and
encoder/configuration destruction. Input is generated 8-bit planar420, with
no file-input parser. Settings are generic C (`cpuid=0`), ultrafast, all-intra,
GOP0, threads0, WPP0, OWF0, one reference, level6.2, default QP22.
The host thread-creation wrapper rejects any attempted encoder worker thread.

## Measured host results

GCC13.3, Linux x86-64; each successful stream was identified by FFprobe and
fully decoded by FFmpeg6.1.1. Main-profile, `yuv420p` output:

| Synthetic input | Frames | HEVC bytes | Peak wrapped requested live bytes |
| --- | ---: | ---: | ---: |
|64×64|3|256|448,512|
|320×192|2|668|949,656|
|3840×2160|1|33,518|48,184,060|
|7680×4320|1|135,067|191,500,060|

All successful cases returned wrapped live allocation count/bytes to zero and
created zero worker threads. These synthetic patterns do not represent normal
photographic bitrate or quality. This is real software HEVC output on the
host, not a 4K/8K camera capture, sensor mode or in-camera video result. Host
durations are recorded as raw nanoseconds in local reports; no camera FPS is
inferred.

Strict harness compilation and small-case AddressSanitizer/UBSan runs passed,
including a1-byte allocation budget, output-size rejection, cancellation
before/between frame calls, malformed width and preservation of an existing
output. Full-size decode tests used the normal host build. No physical ARM
execution occurred.

## Bounds and limitations

Dimensions are bounded to64..8192 and multiples of8; input count1..3 frames.
The caller supplies an allocation-request budget (maximum1GiB), output limit
(maximum64MiB), and deterministic cancel-after-frame boundary. The runner puts
each encoding in a bounded host subprocess with a60-second timeout.

Static-link wrappers cover upstream/harness `malloc`, `calloc`, `realloc`,
`free` and `strdup` calls. The live registry is bounded to16,384 entries
(256KiB host BSS). The reported budget is **requested live bytes**, excluding
that registry, stack, allocator metadata, realloc's transient implementation
storage, libc internal allocations and OS runtime. It is not an RSS cap.
Allocation overflow/budget exhaustion terminates the host subprocess with
code70; upstream has no custom allocator or reliable all-path OOM-unwind API.
An owned incomplete output may remain and is not accepted as a valid result.

Output is created exclusively (`wx`), checked against the limit before chunk
writes, and never replaces an existing host file. Cancellation occurs between
encoder calls; it cannot interrupt one expensive call. No native allocator,
file exporter, cooperative camera scheduler or cancellation adapter is supplied.

## ARM946 compile audit

`arm_audit.py` compiles the generic source for ARM946E-S, soft float,
freestanding, then produces a **relocatable object with unresolved symbols**.
This is not a native executable, usable camera library or throughput test.
Newlib needs explicit POSIX declaration feature macros; the first audit
compiled58/63 files, with missing `semaphore.h` and math-constant visibility
blocking the remainder. Its257,237-byte text and131,906-byte BSS are partial
only and exclude those files and all runtime support.

The optional `--semaphore-declarations` pass supplies an incomplete forward
type plus function declarations for unused semaphore inline helpers and
enables default math declarations. It supplies no POSIX implementation or
pretend semaphore ABI. That pass compiled 63/63 sources: 284,111 bytes of text,
zero data and 131,906 bytes of BSS before runtime libraries. Its report lists
sections and unresolved symbols; relocatable-link success is not a completed
port.

The audit already establishes pthread mutex/rwlock/queue dependencies,
atomics, allocation/stdio, soft-float helpers and libm requirements. GCC's
individual ARM stack frames include153,904 bytes in `kvz_search_lcu`,68,856
in fractional search and42,352 in `kvz_rdoq`; these are not a worst-case call
chain. The existing64KiB film-worker stack cannot run the unchanged first
function. Threads0 does not remove all synchronization or floating math.

## Reproduce in Linux/WSL

Use a fresh clone destination; the scripts verify its HEAD. Outputs always
use fresh ignored directories and are retained for inspection.

```sh
git clone --depth 1 --branch v2.3.2 https://github.com/ultravideo/kvazaar.git \
  vendor/kvazaar-6040962
python3 src/experimental/hevc_kvazaar/run.py --source vendor/kvazaar-6040962 --full
python3 src/experimental/hevc_kvazaar/run.py --source vendor/kvazaar-6040962 --sanitize
python3 src/experimental/hevc_kvazaar/arm_audit.py --source vendor/kvazaar-6040962
python3 src/experimental/hevc_kvazaar/arm_audit.py --source vendor/kvazaar-6040962 \
  --semaphore-declarations
```

Requires Git, CMake≥3.25, GCC/G++, FFmpeg/FFprobe and the GNU ARM embedded
compiler/binutils for the compile audit. No dependency is installed by these
scripts. No native integration is enabled.
