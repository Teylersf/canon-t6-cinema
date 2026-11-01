/* Bounded experimental HEVC I_PCM writer. GPL-2.0-only. */
#ifndef T6_EXPERIMENTAL_HEVC_INTRA_H
#define T6_EXPERIMENTAL_HEVC_INTRA_H
#include <stddef.h>
#include <stdint.h>

#define HI_CTU_BYTES 1536u
#define HI_OUTPUT_BYTES 4096u
#define HI_MAX_DIMENSION 8192u
#define HI_MAX_LUMA_PS UINT32_C(35651584) /* level6.2 coded-picture limit */
#define HI_MAX_FRAMES 16u

typedef enum {
    HI_OK = 0, HI_BAD_ARGUMENT, HI_OUTPUT_LIMIT, HI_SINK_FAILED,
    HI_SOURCE_FAILED, HI_CANCELLED
} hi_result;
typedef enum {
    HI_COLOR_UNSPECIFIED = 0,
    /* Matrix6(BT.601), limited range, centered420; primaries/transfer stay
     * unspecified2 because a sample conversion does not establish them. */
    HI_COLOR_BT601_LIMITED_CENTERED = 1
} hi_color;
typedef struct {
    uint32_t width, height, frames; /* even,2..8192; padded area<=HI_MAX_LUMA_PS */
    uint64_t output_limit; /* includes Annex-B start codes and escaped bytes */
} hi_config;
typedef struct {
    void *context;
    /* Exactly 1 accepts all bytes; any other result stops permanently. */
    int (*write)(void *, const uint8_t *, size_t);
    /* Strict frame-major, row-major CTU order. x/y are multiples of32.
     * Fill Y[1024], Cb[256], Cr[256] in row-major8-bit420 order. Last CTU
     * may extend beyond visible dimensions; callback must pad without an
     * out-of-bounds source read. Its own source/camera lifetime is its duty. */
    int (*read_ctu)(void *, uint32_t frame, uint32_t x, uint32_t y,
                    uint8_t block[HI_CTU_BYTES]);
    /* Optional; checked before every CTU and frame, never in IRQ context. */
    int (*cancelled)(void *);
} hi_io;
typedef struct {
    uint8_t output[HI_OUTPUT_BYTES];
    uint8_t ctu[HI_CTU_BYTES];
} hi_workspace;
typedef struct {
    uint64_t bytes_committed;
    uint32_t frames_encoded, ctus_encoded;
} hi_stats;

/* New standalone software bitstream experiment, NOT camera integration.
 * Emits8-bit420, intra I_PCM, Annex-B VPS/SPS/PPS+IDR pictures. I_PCM carries
 * raw samples: it is deliberately NOT a space-efficient HEVC compressor.
 * No heap, OS calls, threads, sensor access, capture hooks or native recording.
 * No timing/VUI FPS claim. Core workspace5632B, plus a small call stack.
 * Caller serializes calls and callbacks. read_ctu may fill only its supplied
 * CTU span; no other workspace/stats mutation or callback reentry is allowed.
 * A stopped output may contain a partial stream and must not be published as
 * completed. Stats count only accepted output bytes and completed units.
 */
hi_result hi_encode(const hi_config *, const hi_io *, hi_workspace *, hi_stats *);
/* Optional color signaling. The original API is equivalent to UNSPECIFIED
 * and retains its original output. No frame timing is signaled by either. */
hi_result hi_encode_with_color(const hi_config *, const hi_io *, hi_workspace *,
                               hi_stats *, hi_color);
#endif
