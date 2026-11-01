/* Manual synthetic HEVC proof, GPL-2.0-only. No camera frame source. */
#ifndef FILM_HEVC_H
#define FILM_HEVC_H
#include <stdint.h>

#define FILM_HEVC_FRAMES 2u
#define FILM_HEVC_DEADLINE_MS 30000u
#define FILM_HEVC_4K_DEADLINE_MS 120000u
#define FILM_HEVC_8K_DEADLINE_MS 300000u
#define FILM_HEVC_DMA_BYTES (64u * 1024u)
#define FILM_HEVC_REPORT_DEADLINE_MS 5000u
#define FILM_HEVC_REPORT_MAX_BYTES 2048u
#define FILM_HEVC_REPORT_VERIFY_BYTES 256u
#define FILM_HEVC_OPTIMIZATION_PROFILE 1u
#define FILM_HEVC_OPTIMIZATION_LABEL "native_Os_core_O2"
#define FILM_HEVC_OUTPUT_LIMIT (128u * 1024u * 1024u)
#define FILM_HEVC_CODEC_NOT_CALLED UINT32_MAX

enum film_hevc_state {
    FILM_HEVC_IDLE = 0, FILM_HEVC_RUNNING, FILM_HEVC_SAVED,
    FILM_HEVC_CANCELLED, FILM_HEVC_BUSY, FILM_HEVC_DEADLINE,
    FILM_HEVC_NO_MEMORY, FILM_HEVC_BAD_SIZE, FILM_HEVC_IO_ERROR,
    FILM_HEVC_VERIFY_ERROR, FILM_HEVC_CODEC_ERROR, FILM_HEVC_MEMORY_CLASS_ERROR
};

enum film_hevc_phase {
    FILM_HEVC_PHASE_NONE = 0, FILM_HEVC_PHASE_SETUP, FILM_HEVC_PHASE_ENCODE,
    FILM_HEVC_PHASE_TMP_VERIFY, FILM_HEVC_PHASE_RENAME, FILM_HEVC_PHASE_FINAL_VERIFY
};
#define FILM_HEVC_PHASE_COUNT 5u

enum film_hevc_report_state {
    FILM_HEVC_REPORT_NONE = 0, FILM_HEVC_REPORT_RUNNING, FILM_HEVC_REPORT_SAVED,
    FILM_HEVC_REPORT_SKIPPED_GUARD, FILM_HEVC_REPORT_NO_MEMORY,
    FILM_HEVC_REPORT_SLOTS_FULL, FILM_HEVC_REPORT_IO_ERROR,
    FILM_HEVC_REPORT_VERIFY_ERROR, FILM_HEVC_REPORT_DEADLINE,
    FILM_HEVC_REPORT_INTERRUPTED, FILM_HEVC_REPORT_MEMORY_CLASS_ERROR
};

struct film_hevc_results {
    uint32_t version;
    enum film_hevc_state state;
    uint32_t width, height, frames_requested, frames_encoded, ctus_encoded;
    uint32_t bytes, crc32, elapsed_ms, codec_result; /* HI_* or NOT_CALLED sentinel */
    char path[32]; /* nonempty after rename; retained even if final verify fails */
    /* Version2 appends fields; the version1 prefix above retains its layout. */
    uint32_t size_choice, budget_ms, buffer_bytes, optimization_profile;
    uint32_t codec_called, codec_bytes_committed;
    uint32_t accepted_bytes; /* whole codec callbacks returning success; may differ from written on failure */
    uint32_t written_bytes;  /* actual valid positive FIO write returns; same as bytes */
    uint32_t phase_started_mask, phase_completed_mask, stopped_phase;
    uint32_t phase_ms[FILM_HEVC_PHASE_COUNT]; /* setup, encode+FIO+CRC, TMP verify, rename, final verify */
    uint32_t write_callback_calls, fio_write_calls, fio_read_calls, fio_read_bytes;
    enum film_hevc_report_state report_state; /* independent of immutable primary outcome */
    uint32_t report_elapsed_ms, report_bytes;
    char report_path[32]; /* retained after report rename even if its final verification fails */
};

/* Called only by the serialized film worker. Size0=320x180 (boot default),
 * 1=3840x2160,2=7680x4320. Always TWO generated distinct frames, never capture.
 * Cached malloc-owned5632B workspace plus managed64KiB DMA aggregation buffer.
 * Tail flush is mandatory after codec success; no full-frame RAM or aliases.
 * Queue action clears stale cancel; worker MUST NOT clear it after claim.
 * Cooperative budgets30/120/300s by size,128MiB; guards before CTU/callback,
 * actual write/readback/publication. Phase timings include their I/O and CRC.
 * Report uses a fresh idle guard/5s budget after proof allocations are freed;
 * cancel/capture/shutdown skip it. Report failure never changes proof outcome.
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
