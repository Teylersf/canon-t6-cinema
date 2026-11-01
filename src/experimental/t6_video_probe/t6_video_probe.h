/* Isolated fixed-field observation prototype. GPL-2.0-only.
 * No native address map, hardware access, file I/O or recording integration.
 */
#ifndef SE_EXPERIMENTAL_T6_VIDEO_PROBE_H
#define SE_EXPERIMENTAL_T6_VIDEO_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define SE_TVP_MAX_RECORDS 512u
#define SE_TVP_INTERVAL_MS 5u
#define SE_TVP_WINDOW_MS 5000u
#define SE_TVP_ARM_WAIT_MS 120000u

/* Fixed symbolic profile. A future private bridge must map these IDs ONLY to
 * the ROM-verified ordinary low-RAM globals. No address is accepted by this
 * core. Every observed pointer is merely a word VALUE and is never followed.
 * Names describe static uses, not a verified live format/ownership boundary.
 */
typedef enum {
    SE_TVP_H264_INPUT_VALUE = 0,
    SE_TVP_H264_STREAM1_VALUE,
    SE_TVP_H264_READ_HANDLE_VALUE,
    SE_TVP_H264_WRITE_HANDLE_VALUE,
    SE_TVP_MVR_CONTEXT_POINTER_VALUE,
    SE_TVP_PRODUCER_ACTIVE_MASK_VALUE,
    SE_TVP_PRODUCER_YUV_TRACKED_ADDRESS_VALUE,
    SE_TVP_FIELD_COUNT
} se_tvp_field;

typedef enum {
    SE_TVP_OFF = 0, /* zero-initialized state is inactive */
    SE_TVP_ARMED,
    SE_TVP_OBSERVING,
    SE_TVP_DONE
} se_tvp_phase;

typedef enum {
    SE_TVP_STOP_NONE = 0,
    SE_TVP_STOP_RECORDING_ENDED,
    SE_TVP_STOP_WINDOW,
    SE_TVP_STOP_CAPACITY,
    SE_TVP_STOP_CANCEL,
    SE_TVP_STOP_ARM_TIMEOUT,
    SE_TVP_STOP_MODE,
    SE_TVP_STOP_CAPTURE,
    SE_TVP_STOP_SHUTDOWN,
    SE_TVP_STOP_READ_FAILED,
    SE_TVP_STOP_CLOCK,
    SE_TVP_STOP_BAD_INPUT
} se_tvp_stop;

typedef enum {
    SE_TVP_RECORDING_IDLE = 0,
    SE_TVP_RECORDING_PREPARING = 1,
    SE_TVP_RECORDING_ACTIVE = 2
} se_tvp_recording;

#define SE_TVP_MODE_ALLOWED 1u
#define SE_TVP_MODE_MOVIE 2u
#define SE_TVP_MODE_LIVEVIEW 4u
#define SE_TVP_MODE_ALL 7u

#define SE_TVP_RECORD_INCONSISTENT 1u
#define SE_TVP_RECORD_READ_FAILED 2u
#define SE_TVP_RECORD_CLOCK_INVALID 4u
#define SE_TVP_RECORD_WINDOW_CROSSED 8u

typedef struct {
    uint32_t read_start_ms; /* immediately before the first read callback */
    uint32_t read_end_ms;   /* immediately after the last read callback */
    uint32_t first[SE_TVP_FIELD_COUNT];
    uint32_t second[SE_TVP_FIELD_COUNT];
    uint16_t changed_fields; /* bit i: both reads succeeded and differ */
    uint16_t failed_reads;   /* first pass bits0..6, second pass bits7..13 */
    uint32_t flags;
} se_tvp_record;

typedef struct {
    uint32_t now_ms;
    se_tvp_recording recording;
    uint32_t mode_flags; /* ALLOWED is required; MOVIE+LIVEVIEW to sample */
    unsigned cancel;         /* exactly0 or1; checked before any callbacks */
    unsigned capture_active; /* exactly0 or1 */
    unsigned shutdown;       /* exactly0 or1 */
} se_tvp_inputs;

typedef struct {
    void *context;
    /* Return exactly1 on success; any other result marks a failed read.
     * Only the seven enum IDs above are ever supplied. The callback MUST
     * itself enforce its fixed address whitelist; do not cast observed
     * values to pointers or read MMIO, pixels, dynamic targets or aliases.
     */
    int (*read_word)(void *context, se_tvp_field field, uint32_t *value);
    /* Same monotonically advancing modulo-2^32 millisecond domain as now_ms.
     * Called before/after each paired snapshot, never while simply waiting.
     */
    uint32_t (*clock_ms)(void *context);
} se_tvp_io;

/* Inspect phase/reason/count/timestamps; treat all members as read-only after
 * init. No heap or mandatory record array is held in the core. */
typedef struct {
    se_tvp_record *records;
    size_t storage_bytes;
    uint32_t capacity;
    uint32_t count;
    se_tvp_phase phase;
    se_tvp_stop reason;
    uint32_t armed_ms;
    uint32_t observation_start_ms;
    uint32_t last_tick_ms;
    uint32_t last_read_start_ms;
    uint32_t last_read_end_ms;
    uint32_t initialized;
} se_tvp_state;

typedef enum {
    SE_TVP_OK = 0,
    SE_TVP_BAD_ARGUMENT,
    SE_TVP_BAD_STORAGE,
    SE_TVP_BAD_STATE
} se_tvp_result;

/* Caller supplies aligned storage for capacity records, capacity1..512.
 * Rejects insufficient/overflowing spans and overlap with state before any
 * write. Failed init leaves state/storage unchanged. Successful init is OFF
 * and changes no record bytes. Only records[0..count) contain current data.
 */
se_tvp_result se_tvp_init(se_tvp_state *state, se_tvp_record *records,
                         size_t storage_bytes, uint32_t capacity);

/* Explicitly arm from OFF or DONE; reset count/metadata, leave record bytes
 * untouched. No callback, recording request or automatic re-arm occurs. */
se_tvp_result se_tvp_arm(se_tvp_state *state, uint32_t now_ms);

/* Clear state to OFF/count0 while retaining its supplied storage. No record
 * bytes are erased. Cancel via tick instead to keep the completed records. */
se_tvp_result se_tvp_reset(se_tvp_state *state);

/* At most one paired snapshot/tick: seven first-pass reads, then seven
 * second-pass reads, plus two clocks. Never catches up missed intervals.
 * First observation waits for ACTIVE in MOVIE+LIVEVIEW; PREPARING is not
 * sampled. Arm wait expires at120s, observation window at5s, capacity<=512.
 * Sampling starts are requested at least5ms apart. Late/canceled/guarded ticks
 * cannot initiate reads. A callback cannot be preempted: a snapshot begun
 * before the window can finish after it, is flagged, then stops. Invalid or
 * backwards clocks stop observation; all compared gaps must be<2^31ms.
 *
 * OFF and DONE ticks are no-ops even with null inputs/io. A zero-initialized
 * state is OFF; it must be initialized before arm. Waiting/guarded ticks
 * require no io. Missing callbacks on a due sample stop with BAD_INPUT.
 * Inputs/io are copied before state/record writes. The caller must serialize
 * calls, keep metadata/storage valid, and forbid callback reentry/mutation of
 * state/storage. Actual mode/cancel is a caller snapshot, not a hardware check.
 *
 * Equal paired words are NOT proof of an atomic snapshot, buffer ownership,
 * producer readiness, encoder release, format, stride, cache coherence, a
 * processing deadline, exposure count or frame rate. No hook is installed.
 */
se_tvp_result se_tvp_tick(se_tvp_state *state, const se_tvp_inputs *inputs,
                         const se_tvp_io *io);

#endif
