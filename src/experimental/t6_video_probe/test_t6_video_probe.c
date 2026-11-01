/* Independent host tests for the symbolic observation core. No hardware I/O. */
#include "t6_video_probe.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t words[2][SE_TVP_FIELD_COUNT];
    uint32_t clocks[2];
    uint16_t failures;
    int failure_return;
    unsigned reads;
    unsigned clock_calls;
} fake_io;

typedef struct {
    se_tvp_state state;
    se_tvp_record records[SE_TVP_MAX_RECORDS + 1u];
    fake_io fake;
    se_tvp_io io;
} fixture;

static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)

static int fake_read(void *opaque, se_tvp_field field, uint32_t *value)
{
    fake_io *fake = opaque;
    unsigned position = fake->reads % (2u * SE_TVP_FIELD_COUNT);
    unsigned pass = position / SE_TVP_FIELD_COUNT;
    unsigned index = position % SE_TVP_FIELD_COUNT;
    /* All read addresses are represented by these exact fixed IDs. Word
     * contents, including hostile pointer-like values, never become IDs. */
    CHECK((unsigned)field == index);
    CHECK((unsigned)field < SE_TVP_FIELD_COUNT);
    CHECK(value != NULL);
    ++fake->reads;
    *value = fake->words[pass][index];
    if (fake->failures & (uint16_t)(1u << position)) {
        *value = UINT32_C(0xbadbadff);
        return fake->failure_return;
    }
    return 1;
}

static uint32_t fake_clock(void *opaque)
{
    fake_io *fake = opaque;
    unsigned index = fake->clock_calls++;
    CHECK(index < 2u);
    return fake->clocks[index];
}

static void prepare(fixture *f, uint32_t capacity)
{
    unsigned i;
    memset(f, 0, sizeof(*f));
    memset(f->records, 0xa5, sizeof(f->records));
    CHECK(se_tvp_init(&f->state, f->records, sizeof(f->records), capacity) == SE_TVP_OK);
    f->io.context = &f->fake;
    f->io.read_word = fake_read;
    f->io.clock_ms = fake_clock;
    f->fake.failure_return = 0;
    for (i = 0; i < SE_TVP_FIELD_COUNT; ++i) {
        f->fake.words[0][i] = UINT32_MAX - i * UINT32_C(0x10000001);
        f->fake.words[1][i] = f->fake.words[0][i];
    }
}

static se_tvp_inputs input(uint32_t now)
{
    se_tvp_inputs in = {0};
    in.now_ms = now;
    in.recording = SE_TVP_RECORDING_ACTIVE;
    in.mode_flags = SE_TVP_MODE_ALL;
    return in;
}

static se_tvp_result tick(fixture *f, se_tvp_inputs in, uint32_t begin, uint32_t end)
{
    f->fake.clock_calls = 0;
    f->fake.clocks[0] = begin;
    f->fake.clocks[1] = end;
    return se_tvp_tick(&f->state, &in, &f->io);
}

static void expect_untouched(const se_tvp_record *record, size_t count)
{
    const unsigned char *p = (const unsigned char *)record;
    size_t i;
    for (i = 0; i < count * sizeof(*record); ++i) CHECK(p[i] == 0xa5u);
}

static void start(fixture *f, uint32_t now, uint32_t capacity)
{
    prepare(f, capacity);
    CHECK(se_tvp_arm(&f->state, now) == SE_TVP_OK);
    CHECK(tick(f, input(now), now, now) == SE_TVP_OK);
    CHECK(f->state.count == 1u);
}

static void test_inactive_and_initialization(void)
{
    fixture f;
    se_tvp_state blank = {0}, before;
    uintptr_t wrapped;
    size_t alignment;
    struct alignment_probe { char prefix; se_tvp_record record; };
    CHECK(se_tvp_tick(&blank, NULL, NULL) == SE_TVP_OK);
    CHECK(se_tvp_arm(&blank, 0) == SE_TVP_BAD_STATE);
    CHECK(se_tvp_reset(&blank) == SE_TVP_BAD_STATE);
    CHECK(se_tvp_tick(NULL, NULL, NULL) == SE_TVP_BAD_ARGUMENT);
    CHECK(se_tvp_init(NULL, NULL, 0, 0) == SE_TVP_BAD_ARGUMENT);
    memset(&f, 0xa5, sizeof(f));
    before = f.state;
    CHECK(se_tvp_init(&f.state, NULL, sizeof(f.records), 1) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, f.records, sizeof(f.records), 0) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, f.records, sizeof(f.records), 513) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, f.records, sizeof(se_tvp_record) - 1u, 1) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, f.records, sizeof(se_tvp_record) * 511u, 512) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, (se_tvp_record *)((unsigned char *)f.records + 1), sizeof(se_tvp_record), 1) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, (se_tvp_record *)(void *)&f.state, sizeof(se_tvp_record), 1) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, f.records, SIZE_MAX, 1) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init(&f.state, f.records, (size_t)PTRDIFF_MAX + 1u, 1) == SE_TVP_BAD_STORAGE);
    CHECK(se_tvp_init((se_tvp_state *)(UINTPTR_MAX - 3u), f.records, sizeof(se_tvp_record), 1) == SE_TVP_BAD_STORAGE);
    alignment = offsetof(struct alignment_probe, record);
    wrapped = UINTPTR_MAX - (UINTPTR_MAX % alignment);
    CHECK(se_tvp_init(&f.state, (se_tvp_record *)wrapped, sizeof(se_tvp_record), 1) == SE_TVP_BAD_STORAGE);
    CHECK(memcmp(&before, &f.state, sizeof(before)) == 0);
    expect_untouched(f.records, SE_TVP_MAX_RECORDS + 1u);
    prepare(&f, 1);
    CHECK(f.state.phase == SE_TVP_OFF && f.state.count == 0u);
    CHECK(se_tvp_tick(&f.state, NULL, NULL) == SE_TVP_OK);
    expect_untouched(f.records, SE_TVP_MAX_RECORDS + 1u);
    CHECK(se_tvp_arm(&f.state, 55) == SE_TVP_OK);
    CHECK(se_tvp_arm(&f.state, 55) == SE_TVP_BAD_STATE);
    CHECK(f.fake.reads == 0u && f.fake.clock_calls == 0u);
    expect_untouched(f.records, SE_TVP_MAX_RECORDS + 1u);
}

static void test_wait_and_arm_deadline(uint32_t origin)
{
    fixture f;
    se_tvp_inputs in = input(origin);
    prepare(&f, 4);
    CHECK(se_tvp_arm(&f.state, origin) == SE_TVP_OK);
    in.recording = SE_TVP_RECORDING_IDLE;
    in.mode_flags = SE_TVP_MODE_ALLOWED;
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    in.now_ms = origin + 100u;
    in.recording = SE_TVP_RECORDING_PREPARING;
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    in.now_ms = origin + SE_TVP_ARM_WAIT_MS - 1u;
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.phase == SE_TVP_ARMED && f.state.count == 0u);
    in.now_ms = origin + SE_TVP_ARM_WAIT_MS;
    in.recording = SE_TVP_RECORDING_ACTIVE;
    in.mode_flags = SE_TVP_MODE_ALL;
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_ARM_TIMEOUT && f.state.count == 0u);
    CHECK(f.fake.reads == 0u && f.fake.clock_calls == 0u);
    expect_untouched(f.records, SE_TVP_MAX_RECORDS + 1u);
}

static void test_pair_values_and_spacing(void)
{
    fixture f;
    se_tvp_inputs in;
    unsigned i;
    prepare(&f, 8);
    f.fake.words[1][0] ^= UINT32_C(0x80000000);
    f.fake.words[1][6] = 0;
    CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
    CHECK(tick(&f, input(100), 101, 103) == SE_TVP_OK);
    CHECK(f.state.phase == SE_TVP_OBSERVING && f.state.count == 1u);
    CHECK(f.state.observation_start_ms == 101u);
    CHECK(f.records[0].read_start_ms == 101u && f.records[0].read_end_ms == 103u);
    CHECK(f.records[0].changed_fields == ((1u << 0) | (1u << 6)));
    CHECK(f.records[0].failed_reads == 0u);
    CHECK(f.records[0].flags == SE_TVP_RECORD_INCONSISTENT);
    for (i = 0; i < SE_TVP_FIELD_COUNT; ++i) {
        CHECK(f.records[0].first[i] == f.fake.words[0][i]);
        CHECK(f.records[0].second[i] == f.fake.words[1][i]);
    }
    in = input(105);
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.count == 1u && f.fake.reads == 14u);
    CHECK(tick(&f, input(106), 106, 109) == SE_TVP_OK);
    CHECK(f.state.count == 2u && f.fake.reads == 28u);
    CHECK(tick(&f, input(500), 500, 500) == SE_TVP_OK);
    CHECK(f.state.count == 3u && f.fake.reads == 42u);
    in = input(500);
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.count == 3u);
    CHECK(tick(&f, input(505), 505, 505) == SE_TVP_OK);
    CHECK(f.state.count == 4u && f.fake.reads == 56u);
    CHECK(se_tvp_reset(&f.state) == SE_TVP_OK);
    CHECK(f.state.count == 0u && f.state.phase == SE_TVP_OFF);
    CHECK(f.records[0].read_start_ms == 101u);
    CHECK(se_tvp_arm(&f.state, 600) == SE_TVP_OK);
    CHECK(f.state.count == 0u && f.records[0].read_start_ms == 101u);
    expect_untouched(f.records + 4, SE_TVP_MAX_RECORDS - 3u);
}

static void test_guards(void)
{
    fixture f;
    se_tvp_inputs in;
    unsigned phase, guard;
    const se_tvp_stop expected[] = {
        SE_TVP_STOP_CANCEL, SE_TVP_STOP_CAPTURE, SE_TVP_STOP_SHUTDOWN,
        SE_TVP_STOP_MODE, SE_TVP_STOP_MODE, SE_TVP_STOP_MODE
    };
    for (phase = 0; phase < 2u; ++phase) {
        for (guard = 0; guard < sizeof(expected) / sizeof(expected[0]); ++guard) {
            prepare(&f, 8);
            CHECK(se_tvp_arm(&f.state, 10) == SE_TVP_OK);
            if (phase) CHECK(tick(&f, input(10), 10, 10) == SE_TVP_OK);
            /* Guard loss must stop even before the next 5ms sample is due. */
            in = input(11);
            if (guard == 0u) in.cancel = 1;
            if (guard == 1u) in.capture_active = 1;
            if (guard == 2u) in.shutdown = 1;
            if (guard == 3u) in.mode_flags &= ~SE_TVP_MODE_ALLOWED;
            if (guard == 4u) in.mode_flags &= ~SE_TVP_MODE_MOVIE;
            if (guard == 5u) in.mode_flags &= ~SE_TVP_MODE_LIVEVIEW;
            CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
            CHECK(f.state.phase == SE_TVP_DONE && f.state.reason == expected[guard]);
            CHECK(f.state.count == phase && f.fake.reads == phase * 14u);
            CHECK(se_tvp_tick(&f.state, NULL, NULL) == SE_TVP_OK);
            CHECK(f.fake.reads == phase * 14u);
        }
    }
    for (guard = 0; guard < 2u; ++guard) {
        start(&f, 10, 8);
        in = input(11);
        in.recording = guard ? SE_TVP_RECORDING_PREPARING : SE_TVP_RECORDING_IDLE;
        CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
        CHECK(f.state.reason == SE_TVP_STOP_RECORDING_ENDED && f.state.count == 1u);
        CHECK(f.fake.reads == 14u);
    }
}

static void test_window_and_callback_deadlines(uint32_t origin)
{
    fixture f;
    se_tvp_inputs in;
    start(&f, origin, 8);
    CHECK(tick(&f, input(origin + 4999u), origin + 4999u, origin + 4999u) == SE_TVP_OK);
    CHECK(f.state.phase == SE_TVP_OBSERVING && f.state.count == 2u);
    in = input(origin + 5000u);
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_WINDOW && f.state.count == 2u && f.fake.reads == 28u);
    start(&f, origin, 8);
    CHECK(tick(&f, input(origin + 4999u), origin + 5000u, origin + 5000u) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_WINDOW && f.state.count == 1u);
    CHECK(f.fake.reads == 14u && f.fake.clock_calls == 1u);
    start(&f, origin, 8);
    CHECK(tick(&f, input(origin + 4999u), origin + 4999u, origin + 5001u) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_WINDOW && f.state.count == 2u);
    CHECK(f.records[1].flags == SE_TVP_RECORD_WINDOW_CROSSED);
    CHECK(f.records[1].read_end_ms == origin + 5001u && f.fake.reads == 28u);
    prepare(&f, 8);
    CHECK(se_tvp_arm(&f.state, origin) == SE_TVP_OK);
    CHECK(tick(&f, input(origin + 119999u), origin + 120000u, origin + 120000u) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_ARM_TIMEOUT && f.state.count == 0u);
    CHECK(f.fake.clock_calls == 1u && f.fake.reads == 0u);
}

static void test_clock_failures(void)
{
    fixture f;
    se_tvp_inputs in;
    start(&f, 100, 8);
    in = input(99);
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_CLOCK && f.state.count == 1u);
    prepare(&f, 8);
    CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
    CHECK(tick(&f, input(100), 99, 99) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_CLOCK && f.state.count == 0u);
    CHECK(f.fake.clock_calls == 1u && f.fake.reads == 0u);
    prepare(&f, 8);
    CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
    CHECK(tick(&f, input(100), 100, 99) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_CLOCK && f.state.count == 1u);
    CHECK(f.records[0].flags == SE_TVP_RECORD_CLOCK_INVALID);
    start(&f, 100, 8);
    in = input(100u + UINT32_C(0x80000000));
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_CLOCK && f.state.count == 1u);
    prepare(&f, 8);
    CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
    CHECK(tick(&f, input(100), 100, 100u + UINT32_C(0x80000000)) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_CLOCK);
    CHECK(f.records[0].flags == SE_TVP_RECORD_CLOCK_INVALID);
    start(&f, 100, 8);
    CHECK(tick(&f, input(105), 105, 110) == SE_TVP_OK);
    in = input(109);
    CHECK(se_tvp_tick(&f.state, &in, NULL) == SE_TVP_OK);
    CHECK(f.state.reason == SE_TVP_STOP_CLOCK && f.state.count == 2u);
}

static void test_read_failures(void)
{
    fixture f;
    unsigned i;
    const int returns[] = {0, -1, 2};
    for (i = 0; i < sizeof(returns) / sizeof(returns[0]); ++i) {
        prepare(&f, 8);
        f.fake.failure_return = returns[i];
        f.fake.failures = (uint16_t)((1u << 0) | (1u << 8) | (1u << 13));
        f.fake.words[1][0] = 0;
        f.fake.words[1][1] = 0;
        f.fake.words[1][2] ^= 1u;
        f.fake.words[1][6] = 0;
        CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
        CHECK(tick(&f, input(100), 100, 102) == SE_TVP_OK);
        CHECK(f.fake.reads == 14u && f.fake.clock_calls == 2u);
        CHECK(f.state.reason == SE_TVP_STOP_READ_FAILED && f.state.count == 1u);
        CHECK(f.records[0].failed_reads == f.fake.failures);
        CHECK(f.records[0].changed_fields == (1u << 2));
        CHECK(f.records[0].first[0] == 0u);
        CHECK(f.records[0].second[1] == 0u && f.records[0].second[6] == 0u);
        CHECK(f.records[0].flags == (SE_TVP_RECORD_INCONSISTENT | SE_TVP_RECORD_READ_FAILED));
        expect_untouched(f.records + 1, SE_TVP_MAX_RECORDS);
    }
}

static void test_every_failure_and_change_bit(void)
{
    fixture f;
    unsigned bit, i;
    for (bit = 0; bit < 2u * SE_TVP_FIELD_COUNT; ++bit) {
        prepare(&f, 1);
        f.fake.failures = (uint16_t)(1u << bit);
        for (i = 0; i < SE_TVP_FIELD_COUNT; ++i) f.fake.words[1][i] ^= 1u;
        CHECK(se_tvp_arm(&f.state, 0) == SE_TVP_OK);
        CHECK(tick(&f, input(0), 0, 0) == SE_TVP_OK);
        CHECK(f.records[0].failed_reads == (uint16_t)(1u << bit));
        CHECK(f.records[0].changed_fields == (uint16_t)(127u & ~(1u << (bit % SE_TVP_FIELD_COUNT))));
        CHECK(f.state.reason == SE_TVP_STOP_READ_FAILED);
        CHECK(f.fake.reads == 14u);
    }
    prepare(&f, 1);
    f.fake.failures = (uint16_t)((1u << 14) - 1u);
    CHECK(se_tvp_arm(&f.state, 0) == SE_TVP_OK);
    CHECK(tick(&f, input(0), 0, 0) == SE_TVP_OK);
    CHECK(f.records[0].failed_reads == f.fake.failures && f.records[0].changed_fields == 0u);
    CHECK(f.records[0].flags == SE_TVP_RECORD_READ_FAILED);
    for (i = 0; i < SE_TVP_FIELD_COUNT; ++i) {
        CHECK(f.records[0].first[i] == 0u && f.records[0].second[i] == 0u);
    }
}

static void test_metadata_alias(void)
{
    /* Real union members provide alignment for all aliased metadata, without
     * constructing misaligned typed objects inside a byte array. */
    union {
        se_tvp_record records[8];
        struct { se_tvp_inputs in; se_tvp_io io; } metadata;
    } storage;
    fixture reference, aliased;
    se_tvp_record saved;
    prepare(&reference, 8);
    prepare(&aliased, 8);
    memset(&storage, 0xa5, sizeof(storage));
    CHECK(se_tvp_init(&aliased.state, storage.records, sizeof(storage), 8) == SE_TVP_OK);
    CHECK(se_tvp_arm(&reference.state, 10) == SE_TVP_OK);
    CHECK(se_tvp_arm(&aliased.state, 10) == SE_TVP_OK);
    reference.fake.words[1][4] = 0;
    aliased.fake.words[1][4] = 0;
    CHECK(tick(&reference, input(10), 11, 13) == SE_TVP_OK);
    storage.metadata.in = input(10);
    storage.metadata.io = aliased.io;
    aliased.fake.clocks[0] = 11;
    aliased.fake.clocks[1] = 13;
    CHECK(se_tvp_tick(&aliased.state, &storage.metadata.in, &storage.metadata.io) == SE_TVP_OK);
    CHECK(aliased.state.count == 1u && aliased.fake.reads == 14u);
    saved = storage.records[0];
    CHECK(memcmp(&saved, reference.records, sizeof(saved)) == 0);
    expect_untouched(storage.records + 1, 7);
    /* Aliased inputs are also copied before the next output slot is written. */
    storage.metadata.in = input(16);
    storage.metadata.io = aliased.io;
    aliased.fake.clock_calls = 0;
    aliased.fake.clocks[0] = 16;
    aliased.fake.clocks[1] = 17;
    CHECK(se_tvp_tick(&aliased.state, &storage.metadata.in, &storage.metadata.io) == SE_TVP_OK);
    CHECK(aliased.state.count == 2u && storage.records[1].read_start_ms == 16u);
    CHECK(aliased.fake.reads == 28u);
    expect_untouched(storage.records + 2, 6);
}

static void test_capacity(uint32_t capacity, uint32_t origin)
{
    fixture f;
    uint32_t i;
    prepare(&f, capacity);
    CHECK(se_tvp_arm(&f.state, origin) == SE_TVP_OK);
    for (i = 0; i < capacity; ++i) {
        uint32_t now = origin + i * SE_TVP_INTERVAL_MS;
        CHECK(tick(&f, input(now), now, now) == SE_TVP_OK);
        CHECK(f.state.count == i + 1u);
        CHECK(f.state.phase == (i + 1u == capacity ? SE_TVP_DONE : SE_TVP_OBSERVING));
        CHECK(f.records[i].flags == 0u);
    }
    CHECK(f.state.reason == SE_TVP_STOP_CAPACITY);
    CHECK(f.fake.reads == capacity * 14u);
    CHECK(se_tvp_tick(&f.state, NULL, NULL) == SE_TVP_OK);
    expect_untouched(f.records + capacity, SE_TVP_MAX_RECORDS + 1u - capacity);
    CHECK(se_tvp_arm(&f.state, origin + 3000u) == SE_TVP_OK);
    CHECK(f.state.count == 0u && f.state.reason == SE_TVP_STOP_NONE);
    CHECK(f.records[0].read_start_ms == origin);
}

static void test_malformed_inputs(void)
{
    fixture f;
    se_tvp_inputs in;
    unsigned kind;
    for (kind = 0; kind < 7u; ++kind) {
        prepare(&f, 8);
        CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
        in = input(100);
        if (kind == 0u) in.recording = (se_tvp_recording)-1;
        if (kind == 1u) in.recording = (se_tvp_recording)3;
        if (kind == 2u) in.mode_flags |= 8u;
        if (kind == 3u) in.cancel = 2;
        if (kind == 4u) in.capture_active = 2;
        if (kind == 5u) in.shutdown = 2;
        CHECK(se_tvp_tick(&f.state, kind == 6u ? NULL : &in, &f.io) == SE_TVP_BAD_ARGUMENT);
        CHECK(f.state.reason == SE_TVP_STOP_BAD_INPUT && f.state.count == 0u);
        CHECK(f.fake.reads == 0u && f.fake.clock_calls == 0u);
        expect_untouched(f.records, SE_TVP_MAX_RECORDS + 1u);
    }
    for (kind = 0; kind < 3u; ++kind) {
        prepare(&f, 8);
        CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
        in = input(100);
        if (kind == 0u) f.io.read_word = NULL;
        if (kind == 1u) f.io.clock_ms = NULL;
        CHECK(se_tvp_tick(&f.state, &in, kind == 2u ? NULL : &f.io) == SE_TVP_BAD_ARGUMENT);
        CHECK(f.state.reason == SE_TVP_STOP_BAD_INPUT && f.state.count == 0u);
        CHECK(f.fake.reads == 0u && f.fake.clock_calls == 0u);
    }
    for (kind = 0; kind < 5u; ++kind) {
        prepare(&f, 8);
        CHECK(se_tvp_arm(&f.state, 100) == SE_TVP_OK);
        if (kind == 0u) f.state.initialized = 0;
        if (kind == 1u) f.state.capacity = 0;
        if (kind == 2u) f.state.count = 9;
        if (kind == 3u) f.state.records = NULL;
        if (kind == 4u) f.state.phase = (se_tvp_phase)99;
        in = input(100);
        CHECK(se_tvp_tick(&f.state, &in, &f.io) == SE_TVP_BAD_STATE);
        CHECK(f.fake.reads == 0u && f.fake.clock_calls == 0u);
    }
}

int main(void)
{
    test_inactive_and_initialization();
    test_wait_and_arm_deadline(0);
    test_wait_and_arm_deadline(UINT32_MAX - 60000u);
    test_pair_values_and_spacing();
    test_guards();
    test_window_and_callback_deadlines(0);
    test_window_and_callback_deadlines(UINT32_MAX - 2000u);
    test_clock_failures();
    test_read_failures();
    test_every_failure_and_change_bit();
    test_metadata_alias();
    test_capacity(1, 0);
    test_capacity(SE_TVP_MAX_RECORDS, UINT32_MAX - 1000u);
    test_malformed_inputs();
    printf("t6_video_probe: %u assertions passed; symbolic callbacks only\n", checks);
    return 0;
}
