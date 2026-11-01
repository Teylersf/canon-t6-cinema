/* Experimental, camera-independent packed-YUV streak prototype. GPL-2.0-only.
 * No camera hooks, DMA/cache operations, allocations or timing guarantees.
 * This is a horizontal highlight effect, not an optical anamorphic simulation.
 */
#ifndef SE_EXPERIMENTAL_YUV_ANAMORPHIC_H
#define SE_EXPERIMENTAL_YUV_ANAMORPHIC_H

#include <stddef.h>
#include <stdint.h>

#define SE_YA_MAX_WIDTH 8192u
#define SE_YA_MAX_HEIGHT 8192u

typedef enum {
    SE_YA_YUYV = 1, /* bytes Y0, Cb, Y1, Cr for each two pixels */
    SE_YA_UYVY = 2  /* bytes Cb, Y0, Cr, Y1 for each two pixels */
} se_ya_layout;

typedef enum {
    SE_YA_FULL_RANGE = 1,   /* nominal Y/Cb/Cr codes 0..255 */
    SE_YA_STUDIO_RANGE = 2  /* nominal Y 16..235, Cb/Cr 16..240 */
} se_ya_range;

typedef enum {
    SE_YA_CHROMA_UNSIGNED_128 = 1, /* stored 128 is neutral */
    SE_YA_CHROMA_SIGNED_0 = 2     /* two's-complement bytes; stored 0 is neutral */
} se_ya_chroma;

typedef enum {
    SE_YA_NOT_OWNED = 0,
    SE_YA_EXCLUSIVE_CPU = 1
} se_ya_ownership;

typedef enum {
    SE_YA_OK = 0,
    SE_YA_BAD_DESCRIPTOR = 1,
    SE_YA_BAD_PARAMS = 2,
    SE_YA_BAD_OWNERSHIP = 3,
    SE_YA_SCRATCH_TOO_SMALL = 4,
    SE_YA_OVERLAP = 5
} se_ya_result;

typedef struct {
    uint8_t *data;
    size_t bytes;    /* accessible allocation; <=PTRDIFF_MAX, interval cannot wrap */
    size_t stride;   /* positive bytes between row starts, >= width*2 */
    uint32_t width;  /* even, 2..8192; partial chroma pairs are rejected */
    uint32_t height; /* 1..8192 */
    se_ya_layout layout;
    se_ya_range range;
    se_ya_chroma chroma;
    se_ya_ownership ownership;
} se_ya_frame;

typedef struct {
    uint16_t strength_q8;      /* 0..256: 0 is exact bypass */
    uint16_t gain;             /* 0..8: 0 is exact bypass */
    uint16_t threshold;        /* nominal Y code in the chosen frame range */
    uint8_t short_decay_shift; /* 1..8 */
    uint8_t long_decay_shift;  /* short_decay_shift..8 */
    uint16_t tint_strength_q8; /* 0..256: 0 preserves all chroma */
    uint16_t tint_cb;          /* target logical unsigned code; 128 is neutral */
    uint16_t tint_cr;          /* target logical unsigned code; 128 is neutral */
} se_ya_params;

/* Explicit recipes: 0 Off, 1 Subtle, 2 Cinema, 3 Strong; tint 0 Blue, 1 Amber.
 * Targets are always logical unsigned-128 chroma, even with signed storage.
 * Invalid arguments return BAD_PARAMS without changing *params. */
se_ya_result se_ya_preset(se_ya_params *params, se_ya_range range,
                        unsigned preset, unsigned tint);

/* Returns 2*width bytes or zero for invalid width. Scratch is byte-addressed:
 * it may be unaligned and has no effective-type/alignment precondition. */
size_t se_ya_scratch_bytes(uint32_t width);

/* In-place, deterministic row-local processing. Scratch is caller-provided,
 * exactly bounded by scratch_bytes, O(width), independent of height. Active
 * processing requires se_ya_scratch_bytes(width); bypass needs no scratch.
 * Rejects invalid descriptors/parameters/ownership, insufficient scratch and
 * overlapping frame/scratch allocations BEFORE changing either allocation.
 * Padding/trailing allocation bytes are never read or changed. Odd heights
 * are valid; odd widths and negative/interlaced/tiled/planar layouts are not.
 *
 * Range behavior: highlights use Y clamped to the nominal range. During active
 * processing, luma bytes outside that range are preserved; legal Y receives a
 * screen-style lift bounded by nominal white. Chroma outside its nominal range
 * is preserved. Legal chroma moves toward the explicit target, using the mean
 * effect strength of the two legal luma samples sharing it. If one luma is out
 * of range its strength contributes zero; shared chroma may still change due
 * to the other pixel. Full range has no excluded byte codes.
 *
 * EXCLUSIVE_CPU is only a caller assertion: the caller must establish and
 * retain exclusive writable ownership for the entire call, with no producer,
 * encoder, DMA, display or other CPU accessing the frame/scratch. This flag
 * does not verify ownership, cache coherency, memory mapping or byte accuracy.
 * Actual T6 movie input format/representation/stride/lifetime remain UNKNOWN.
 */
se_ya_result se_ya_apply(const se_ya_frame *frame, const se_ya_params *params,
                        void *scratch, size_t scratch_bytes);

#endif
