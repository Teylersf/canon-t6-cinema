/* Manual synthetic HEVC proof, GPL-2.0-only. No camera frame source. */
#ifndef FILM_HEVC_H
#define FILM_HEVC_H
#include <stdint.h>

#define FILM_HEVC_FRAMES 2u
#define FILM_HEVC_DEADLINE_MS 30000u
#define FILM_HEVC_OUTPUT_LIMIT (128u * 1024u * 1024u)
#define FILM_HEVC_CODEC_NOT_CALLED UINT32_MAX

enum film_hevc_state {
    FILM_HEVC_IDLE = 0, FILM_HEVC_RUNNING, FILM_HEVC_SAVED,
    FILM_HEVC_CANCELLED, FILM_HEVC_BUSY, FILM_HEVC_DEADLINE,
    FILM_HEVC_NO_MEMORY, FILM_HEVC_BAD_SIZE, FILM_HEVC_IO_ERROR,
    FILM_HEVC_VERIFY_ERROR, FILM_HEVC_CODEC_ERROR
};

struct film_hevc_results {
    uint32_t version;
    enum film_hevc_state state;
    uint32_t width, height, frames_requested, frames_encoded, ctus_encoded;
    uint32_t bytes, crc32, elapsed_ms, codec_result; /* HI_* or NOT_CALLED sentinel */
    char path[32]; /* nonempty after rename; retained even if final verify fails */
};

/* Called only by the serialized film worker. Size0=320x180 (boot default),
 * 1=3840x2160,2=7680x4320. Always TWO generated distinct frames, never capture.
 * Cached malloc-owned5632B codec workspace; streaming FIO, no full-frame RAM.
 * Queue action clears stale cancel; worker MUST NOT clear it after claim.
 * Cooperative30s/128MiB bound, guards before CTU/write/readback/publication.
 * 0=verified HVC saved,1=cancel/busy/deadline,-1=error. Stop before publication
 * removes only this job's owned TMP. A published HVC is preserved on any later
 * failure and must not be described as verified when state!=SAVED.
 */
int film_hevc_generate(unsigned size_choice);
const struct film_hevc_results *film_hevc_get_results(void);

/* Expected visible 8-bit planar420 samples for independent decoding checks:
 * Y=(3*x+5*y+29*frame)&255; Cb=(64+7*x+3*y+11*frame)&255;
 * Cr=(192+2*x+9*y+13*frame)&255. x/y are coordinates in the given plane.
 * Encoder CTU padding uses the same formula beyond visible dimensions.
 */
uint8_t film_hevc_sample(unsigned frame, unsigned plane, uint32_t x, uint32_t y);
#endif
