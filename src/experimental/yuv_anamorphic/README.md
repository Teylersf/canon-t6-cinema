# Experimental YUV highlight streaks

This standalone C99 prototype was written for T6 Cinema after reviewing the
existing RGB still-photo effect. It uses a separate, row-local algorithm:
highlight extraction, two horizontal decay scales in both directions,
screen-style luma lift, and shared-pair chroma tint. It produces stylized
streaks; it does not simulate optical anamorphic bokeh, change field of view,
or desqueeze an image. Source license: GPL-2.0-only.

**The actual T6 encoder format, stride, buffer ownership and available encoder processing time
remain unknown.** There is no camera frame hook or r11 integration. The newer
diagnostic build links this kernel only for an offscreen test on owned synthetic
pixels. Host tests, ARM compilation and those kernel timings establish no
recording FPS.

The owner's r12 uncached, size-optimized benchmark completed with medians of
89 ms at 320×180, 388 ms at 640×360 and 510 ms at 640×480. See the
[physical baseline](../../../docs/HARDWARE_BASELINE.md) for all samples and
limits. Cached memory and optimized compilation have not yet been physically
compared; these initial timings are not a maximum throughput measurement.

## Explicit input contracts

| Choice | Supported values |
| --- | --- |
| Packed 4:2:2 layout | `SE_YA_YUYV`: Y0, Cb, Y1, Cr; `SE_YA_UYVY`: Cb, Y0, Cr, Y1 |
| Nominal range | Full: Y/Cb/Cr 0–255; studio: Y 16–235, Cb/Cr 16–240 |
| Chroma storage | Unsigned with neutral byte 128; signed two's-complement with neutral byte 0 |
| Dimensions | Even width 2–8192; height 1–8192, including odd heights |

There is no format detection. Tint parameters always use logical unsigned
chroma centered at 128; signed storage is normalized with XOR `0x80`.
Planar, tiled, negative-stride and interlaced layouts are unsupported.

Supply the accessible allocation size and a positive stride of at least
`2 * width`. The allocation must contain `(height - 1) * stride + 2 * width`
bytes, fit `PTRDIFF_MAX`, and have a non-wrapping pointer interval. All
descriptor, parameter, scratch-size and overlap checks precede mutation.
Row padding and trailing allocation bytes are never read or changed.

`SE_YA_EXCLUSIVE_CPU` asserts that the caller has exclusive writable ownership
of frame and scratch for the entire call. The flag cannot verify DMA/encoder
lifetime, cache coherency, mapping or other concurrent access. Establish those
conditions independently before any camera integration.

During active studio-range processing, extraction clamps Y to nominal bounds.
Luma outside those bounds and chroma outside 16–240 remain unchanged. Legal
luma lifts toward nominal white; legal chroma moves toward the selected tint.
Chroma is shared by two pixels: one legal partner can change their shared
chroma even when the other partner's out-of-range luma is preserved.

## Usage and scratch

Active processing needs `2 * width` caller-provided scratch bytes:
3,840 bytes at width 1920 or 2,560 at width 1280. Scratch is byte-addressed,
may be unaligned, cannot overlap the declared frame allocation, and does not
grow with height. No heap, full-frame intermediate, floating point or RGB
conversion is used.

For an already owned generic YUYV frame, with caller-provided variables:

```c
#include "yuv_anamorphic.h"

se_ya_frame frame = {
    data, bytes, stride, width, height,
    SE_YA_YUYV, SE_YA_STUDIO_RANGE,
    SE_YA_CHROMA_UNSIGNED_128, SE_YA_EXCLUSIVE_CPU
};
se_ya_params params;
se_ya_result result = se_ya_preset(&params, frame.range, 2, 0);
if (result == SE_YA_OK)
    result = se_ya_apply(&frame, &params, scratch, scratch_bytes);
```

Presets are 0 Off, 1 Subtle, 2 Cinema, 3 Strong; tint is 0 Blue or 1 Amber.
Handle every nonzero result. Invalid requests leave frame and scratch intact.
Zero strength, zero gain or a threshold at nominal white gives a bit-exact
bypass requiring no scratch; descriptor, parameters and ownership still must
be valid. See the header for parameter limits and return codes.

## Build and validation

From a Linux/WSL shell with a host C compiler, GNU Make and the ARM bare-metal
toolchain installed:

```sh
cd src/experimental/yuv_anamorphic
make test
make asan
make arm
```

`test` builds the real backend with strict C99 warnings. `asan` adds AddressSanitizer
and UndefinedBehaviorSanitizer with failures stopping the run. `arm` compiles
with `-mcpu=arm946e-s -marm -O2 -ffreestanding -fno-builtin`, then reports sections
and undefined symbols. Targets use temporary output directories.

Local checks passed 11 contract groups plus 300 deterministic random frames,
including poisoned padding, minimal spans, metadata aliasing, malformed
descriptors, arithmetic extremes and both chroma representations. The observed
ARM object had 2,268 bytes of code plus 24 bytes of constants, no data/BSS,
and only `__aeabi_uidiv` unresolved for once-per-frame descriptor validation.
The pixel loop has no variable division. Sizes depend on the toolchain;
these are compilation results, not camera throughput measurements.
