#include "t6_video_probe.h"

#define SE_TVP_COOKIE UINT32_C(0x54565031)
#define SE_TVP_HALF_CLOCK UINT32_C(0x80000000)

struct se_tvp_record_alignment {
    char prefix;
    se_tvp_record value;
};

static int se_tvp_span(uintptr_t start, size_t bytes, uintptr_t *end)
{
    if (bytes > (size_t)PTRDIFF_MAX || bytes > UINTPTR_MAX - start) return 0;
    *end = start + bytes;
    return 1;
}

static int se_tvp_ready(const se_tvp_state *state)
{
    return state && state->initialized == SE_TVP_COOKIE &&
           state->capacity && state->capacity <= SE_TVP_MAX_RECORDS &&
           state->count <= state->capacity && state->records;
}

static se_tvp_result se_tvp_stop_now(se_tvp_state *state, se_tvp_stop reason)
{
    state->reason = reason;
    state->phase = SE_TVP_DONE;
    return SE_TVP_OK;
}

se_tvp_result se_tvp_init(se_tvp_state *state, se_tvp_record *records,
                         size_t storage_bytes, uint32_t capacity)
{
    uintptr_t state_begin, state_end, records_begin, records_end;
    size_t required;
    se_tvp_state clean = {0};
    if (!state) return SE_TVP_BAD_ARGUMENT;
    if (!records || !capacity || capacity > SE_TVP_MAX_RECORDS)
        return SE_TVP_BAD_STORAGE;
    required = (size_t)capacity * sizeof(*records);
    state_begin = (uintptr_t)(void *)state;
    records_begin = (uintptr_t)(void *)records;
    if (storage_bytes < required ||
        records_begin % offsetof(struct se_tvp_record_alignment, value) ||
        !se_tvp_span(state_begin, sizeof(*state), &state_end) ||
        !se_tvp_span(records_begin, storage_bytes, &records_end) ||
        (state_begin < records_end && records_begin < state_end))
        return SE_TVP_BAD_STORAGE;
    clean.records = records;
    clean.storage_bytes = storage_bytes;
    clean.capacity = capacity;
    clean.initialized = SE_TVP_COOKIE;
    *state = clean;
    return SE_TVP_OK;
}

se_tvp_result se_tvp_reset(se_tvp_state *state)
{
    if (!se_tvp_ready(state)) return SE_TVP_BAD_STATE;
    state->count = 0;
    state->phase = SE_TVP_OFF;
    state->reason = SE_TVP_STOP_NONE;
    state->armed_ms = 0;
    state->observation_start_ms = 0;
    state->last_tick_ms = 0;
    state->last_read_start_ms = 0;
    state->last_read_end_ms = 0;
    return SE_TVP_OK;
}

se_tvp_result se_tvp_arm(se_tvp_state *state, uint32_t now_ms)
{
    if (!se_tvp_ready(state) ||
        (state->phase != SE_TVP_OFF && state->phase != SE_TVP_DONE))
        return SE_TVP_BAD_STATE;
    (void)se_tvp_reset(state);
    state->armed_ms = now_ms;
    state->last_tick_ms = now_ms;
    state->phase = SE_TVP_ARMED;
    return SE_TVP_OK;
}

se_tvp_result se_tvp_tick(se_tvp_state *state, const se_tvp_inputs *inputs,
                         const se_tvp_io *io)
{
    se_tvp_inputs in;
    se_tvp_io callbacks = {0};
    se_tvp_record record = {0};
    uint32_t begin, end, i, pass;
    int first_sample;
    if (!state) return SE_TVP_BAD_ARGUMENT;
    if (state->phase == SE_TVP_OFF || state->phase == SE_TVP_DONE)
        return SE_TVP_OK;
    if (!se_tvp_ready(state) ||
        (state->phase != SE_TVP_ARMED && state->phase != SE_TVP_OBSERVING))
        return SE_TVP_BAD_STATE;
    if (!inputs) {
        (void)se_tvp_stop_now(state, SE_TVP_STOP_BAD_INPUT);
        return SE_TVP_BAD_ARGUMENT;
    }
    in = *inputs;
    if (io) callbacks = *io;
    if ((unsigned)in.recording > (unsigned)SE_TVP_RECORDING_ACTIVE ||
        (in.mode_flags & ~SE_TVP_MODE_ALL) || in.cancel > 1u ||
        in.capture_active > 1u || in.shutdown > 1u) {
        (void)se_tvp_stop_now(state, SE_TVP_STOP_BAD_INPUT);
        return SE_TVP_BAD_ARGUMENT;
    }
    if (in.cancel) return se_tvp_stop_now(state, SE_TVP_STOP_CANCEL);
    if (in.shutdown) return se_tvp_stop_now(state, SE_TVP_STOP_SHUTDOWN);
    if (in.capture_active) return se_tvp_stop_now(state, SE_TVP_STOP_CAPTURE);
    if (!(in.mode_flags & SE_TVP_MODE_ALLOWED))
        return se_tvp_stop_now(state, SE_TVP_STOP_MODE);
    if ((uint32_t)(in.now_ms - state->last_tick_ms) >= SE_TVP_HALF_CLOCK ||
        (state->count &&
         (uint32_t)(in.now_ms - state->last_read_end_ms) >= SE_TVP_HALF_CLOCK))
        return se_tvp_stop_now(state, SE_TVP_STOP_CLOCK);
    state->last_tick_ms = in.now_ms;
    first_sample = state->phase == SE_TVP_ARMED;
    if (first_sample) {
        if ((uint32_t)(in.now_ms - state->armed_ms) >= SE_TVP_ARM_WAIT_MS)
            return se_tvp_stop_now(state, SE_TVP_STOP_ARM_TIMEOUT);
        if (in.recording != SE_TVP_RECORDING_ACTIVE) return SE_TVP_OK;
    } else {
        if (in.recording != SE_TVP_RECORDING_ACTIVE)
            return se_tvp_stop_now(state, SE_TVP_STOP_RECORDING_ENDED);
    }
    if ((in.mode_flags & (SE_TVP_MODE_MOVIE | SE_TVP_MODE_LIVEVIEW)) !=
        (SE_TVP_MODE_MOVIE | SE_TVP_MODE_LIVEVIEW))
        return se_tvp_stop_now(state, SE_TVP_STOP_MODE);
    if (!first_sample) {
        if ((uint32_t)(in.now_ms - state->observation_start_ms) >= SE_TVP_WINDOW_MS)
            return se_tvp_stop_now(state, SE_TVP_STOP_WINDOW);
        if (state->count >= state->capacity)
            return se_tvp_stop_now(state, SE_TVP_STOP_CAPACITY);
        if ((uint32_t)(in.now_ms - state->last_read_start_ms) < SE_TVP_INTERVAL_MS)
            return SE_TVP_OK;
    }
    if (!io) {
        (void)se_tvp_stop_now(state, SE_TVP_STOP_BAD_INPUT);
        return SE_TVP_BAD_ARGUMENT;
    }
    if (!callbacks.read_word || !callbacks.clock_ms) {
        (void)se_tvp_stop_now(state, SE_TVP_STOP_BAD_INPUT);
        return SE_TVP_BAD_ARGUMENT;
    }
    begin = callbacks.clock_ms(callbacks.context);
    if ((uint32_t)(begin - in.now_ms) >= SE_TVP_HALF_CLOCK)
        return se_tvp_stop_now(state, SE_TVP_STOP_CLOCK);
    if (first_sample) {
        if ((uint32_t)(begin - state->armed_ms) >= SE_TVP_ARM_WAIT_MS)
            return se_tvp_stop_now(state, SE_TVP_STOP_ARM_TIMEOUT);
        state->observation_start_ms = begin;
        state->phase = SE_TVP_OBSERVING;
    } else if ((uint32_t)(begin - state->observation_start_ms) >= SE_TVP_WINDOW_MS) {
        return se_tvp_stop_now(state, SE_TVP_STOP_WINDOW);
    }
    record.read_start_ms = begin;
    for (pass = 0; pass < 2u; ++pass) {
        for (i = 0; i < (uint32_t)SE_TVP_FIELD_COUNT; ++i) {
            uint32_t value = 0;
            uint32_t bit = i + pass * (uint32_t)SE_TVP_FIELD_COUNT;
            if (callbacks.read_word(callbacks.context, (se_tvp_field)i, &value) != 1) {
                record.failed_reads = (uint16_t)(record.failed_reads | (uint16_t)(1u << bit));
                value = 0;
            }
            if (!pass) record.first[i] = value;
            else record.second[i] = value;
        }
    }
    end = callbacks.clock_ms(callbacks.context);
    record.read_end_ms = end;
    for (i = 0; i < (uint32_t)SE_TVP_FIELD_COUNT; ++i) {
        uint32_t both = (1u << i) | (1u << (i + (uint32_t)SE_TVP_FIELD_COUNT));
        if (!(record.failed_reads & both) && record.first[i] != record.second[i])
            record.changed_fields = (uint16_t)(record.changed_fields | (uint16_t)(1u << i));
    }
    if (record.changed_fields) record.flags |= SE_TVP_RECORD_INCONSISTENT;
    if (record.failed_reads) record.flags |= SE_TVP_RECORD_READ_FAILED;
    if ((uint32_t)(end - begin) >= SE_TVP_HALF_CLOCK)
        record.flags |= SE_TVP_RECORD_CLOCK_INVALID;
    else if ((uint32_t)(end - state->observation_start_ms) >= SE_TVP_WINDOW_MS)
        record.flags |= SE_TVP_RECORD_WINDOW_CROSSED;
    state->records[state->count] = record;
    ++state->count;
    state->last_read_start_ms = begin;
    state->last_read_end_ms = end;
    if (record.flags & SE_TVP_RECORD_CLOCK_INVALID)
        return se_tvp_stop_now(state, SE_TVP_STOP_CLOCK);
    if (record.failed_reads)
        return se_tvp_stop_now(state, SE_TVP_STOP_READ_FAILED);
    if (record.flags & SE_TVP_RECORD_WINDOW_CROSSED)
        return se_tvp_stop_now(state, SE_TVP_STOP_WINDOW);
    if (state->count >= state->capacity)
        return se_tvp_stop_now(state, SE_TVP_STOP_CAPACITY);
    return SE_TVP_OK;
}
