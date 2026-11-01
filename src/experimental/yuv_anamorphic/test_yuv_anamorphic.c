/* Independent host contract tests for the experimental YUV backend.
 * GPL-2.0-only. No camera, native firmware or device access. */
#include "yuv_anamorphic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#endif

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); \
} } while (0)

static unsigned checks;
static uint32_t random_state = 0x736f7570u;

static uint32_t next_random(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static uint8_t encode_chroma(unsigned value, se_ya_chroma representation)
{
    return (uint8_t)(value ^ (representation == SE_YA_CHROMA_SIGNED_0 ? 128u : 0u));
}

static unsigned decode_chroma(uint8_t value, se_ya_chroma representation)
{
    return value ^ (representation == SE_YA_CHROMA_SIGNED_0 ? 128u : 0u);
}

static uint8_t *pair_at(const se_ya_frame *frame, uint32_t x, uint32_t y)
{
    return frame->data + (size_t)y * frame->stride + (size_t)(x / 2u) * 4u;
}

static void set_pair(const se_ya_frame *frame, uint32_t x, uint32_t y,
                     unsigned y0, unsigned y1, unsigned cb, unsigned cr)
{
    uint8_t *p = pair_at(frame, x, y);
    unsigned luma = frame->layout == SE_YA_YUYV ? 0u : 1u;
    unsigned chroma = luma ^ 1u;
    p[luma] = (uint8_t)y0;
    p[luma + 2u] = (uint8_t)y1;
    p[chroma] = encode_chroma(cb, frame->chroma);
    p[chroma + 2u] = encode_chroma(cr, frame->chroma);
}

static unsigned get_y(const se_ya_frame *frame, uint32_t x, uint32_t y)
{
    uint8_t *p = pair_at(frame, x, y);
    return p[(frame->layout == SE_YA_YUYV ? 0u : 1u) + (x % 2u) * 2u];
}

static unsigned get_c(const se_ya_frame *frame, uint32_t x, uint32_t y, unsigned cr)
{
    uint8_t *p = pair_at(frame, x, y);
    return decode_chroma(p[(frame->layout == SE_YA_YUYV ? 1u : 0u) + cr * 2u], frame->chroma);
}

static se_ya_frame descriptor(uint8_t *data, size_t bytes, uint32_t width,
                              uint32_t height, size_t stride)
{
    se_ya_frame result;
    result.data = data;
    result.bytes = bytes;
    result.stride = stride;
    result.width = width;
    result.height = height;
    result.layout = SE_YA_YUYV;
    result.range = SE_YA_FULL_RANGE;
    result.chroma = SE_YA_CHROMA_UNSIGNED_128;
    result.ownership = SE_YA_EXCLUSIVE_CPU;
    return result;
}

static se_ya_params strong(se_ya_range range, unsigned tint)
{
    se_ya_params result;
    CHECK(se_ya_preset(&result, range, 3u, tint) == SE_YA_OK);
    result.strength_q8 = 256;
    result.gain = 8;
    result.tint_strength_q8 = 256;
    return result;
}

static void padding_unchanged(const uint8_t *before, const se_ya_frame *frame)
{
    size_t index;
    for (index = 0; index < frame->bytes; ++index) {
        size_t y = index / frame->stride;
        size_t x = index % frame->stride;
        if (y >= frame->height || x >= (size_t)frame->width * 2u)
            CHECK(before[index] == frame->data[index]);
    }
}

static void test_presets_and_sizing(void)
{
    se_ya_params value, before;
    unsigned range, recipe, tint;
    CHECK(se_ya_scratch_bytes(0) == 0);
    CHECK(se_ya_scratch_bytes(1) == 0);
    CHECK(se_ya_scratch_bytes(3) == 0);
    CHECK(se_ya_scratch_bytes(UINT32_MAX) == 0);
    CHECK(se_ya_scratch_bytes(SE_YA_MAX_WIDTH + 2u) == 0);
    CHECK(se_ya_scratch_bytes(2) == 4);
    CHECK(se_ya_scratch_bytes(SE_YA_MAX_WIDTH) == SE_YA_MAX_WIDTH * 2u);
    for (range = 1; range <= 2; ++range)
        for (recipe = 0; recipe <= 3; ++recipe)
            for (tint = 0; tint <= 1; ++tint) {
                CHECK(se_ya_preset(&value, (se_ya_range)range, recipe, tint) == SE_YA_OK);
                CHECK(value.strength_q8 <= 256 && value.gain <= 8);
                CHECK(value.short_decay_shift >= 1 && value.short_decay_shift <= value.long_decay_shift);
                CHECK(value.long_decay_shift <= 8 && value.tint_strength_q8 <= 256);
                CHECK(value.threshold >= (range == 2 ? 16 : 0));
                CHECK(value.threshold <= (range == 2 ? 235 : 255));
                CHECK(value.tint_cb >= (range == 2 ? 16 : 0));
                CHECK(value.tint_cb <= (range == 2 ? 240 : 255));
                CHECK(value.tint_cr >= (range == 2 ? 16 : 0));
                CHECK(value.tint_cr <= (range == 2 ? 240 : 255));
                CHECK(recipe != 0 || value.strength_q8 == 0);
            }
    memset(&value, 0xa6, sizeof(value));
    before = value;
    CHECK(se_ya_preset(&value, (se_ya_range)0, 1, 0) == SE_YA_BAD_PARAMS);
    CHECK(memcmp(&value, &before, sizeof(value)) == 0);
    CHECK(se_ya_preset(&value, SE_YA_FULL_RANGE, 4, 0) == SE_YA_BAD_PARAMS);
    CHECK(memcmp(&value, &before, sizeof(value)) == 0);
    CHECK(se_ya_preset(&value, SE_YA_FULL_RANGE, 1, 2) == SE_YA_BAD_PARAMS);
    CHECK(memcmp(&value, &before, sizeof(value)) == 0);
    CHECK(se_ya_preset(NULL, SE_YA_FULL_RANGE, 1, 0) == SE_YA_BAD_PARAMS);
    ++checks;
}

static void test_bypass(void)
{
    uint8_t storage[256], original[256];
    se_ya_frame frame = descriptor(storage + 1, 251, 10, 3, 29);
    unsigned range, why, i;
    for (i = 0; i < sizeof(storage); ++i) storage[i] = (uint8_t)next_random();
    memcpy(original, storage, sizeof(storage));
    for (range = 1; range <= 2; ++range)
        for (why = 0; why < 3; ++why) {
            se_ya_params params = strong((se_ya_range)range, 0);
            frame.range = (se_ya_range)range;
            if (why == 0) params.strength_q8 = 0;
            if (why == 1) params.gain = 0;
            if (why == 2) params.threshold = (uint16_t)(range == 2 ? 235 : 255);
            CHECK(se_ya_apply(&frame, &params, NULL, 0) == SE_YA_OK);
            CHECK(memcmp(storage, original, sizeof(storage)) == 0);
        }
    ++checks;
}

static void fill_pattern(const se_ya_frame *frame)
{
    uint32_t y, x;
    unsigned ymin = frame->range == SE_YA_STUDIO_RANGE ? 16u : 0u;
    unsigned ymax = frame->range == SE_YA_STUDIO_RANGE ? 235u : 255u;
    for (y = 0; y < frame->height; ++y)
        for (x = 0; x < frame->width; x += 2u) {
            unsigned first = ymin + (x * 7u + y * 19u) % (ymax - ymin + 1u);
            unsigned second = ymin + (x * 13u + y * 11u) % (ymax - ymin + 1u);
            if (x == 12u && y == 1u) first = ymax;
            set_pair(frame, x, y, first, second, 128u, 128u);
        }
}

static void test_layout_and_representation_equivalence(void)
{
    uint8_t storage[4][1024], before[4][1024], scratch[4][96];
    se_ya_frame frames[4];
    unsigned range, combination;
    uint32_t y, x;
    for (range = 1; range <= 2; ++range) {
        se_ya_params params = strong((se_ya_range)range, 0);
        for (combination = 0; combination < 4; ++combination) {
            memset(storage[combination], 0x9d, sizeof(storage[combination]));
            memset(scratch[combination], 0xb7, sizeof(scratch[combination]));
            frames[combination] = descriptor(storage[combination] + 1, 1000, 32, 5, 73);
            frames[combination].range = (se_ya_range)range;
            frames[combination].layout = combination & 1u ? SE_YA_UYVY : SE_YA_YUYV;
            frames[combination].chroma = combination & 2u ? SE_YA_CHROMA_SIGNED_0 : SE_YA_CHROMA_UNSIGNED_128;
            fill_pattern(&frames[combination]);
            memcpy(before[combination], storage[combination], sizeof(storage[combination]));
            CHECK(se_ya_apply(&frames[combination], &params, scratch[combination] + 1, 64) == SE_YA_OK);
            CHECK(storage[combination][0] == 0x9d && storage[combination][1001] == 0x9d);
            padding_unchanged(before[combination] + 1, &frames[combination]);
            CHECK(scratch[combination][0] == 0xb7);
            for (x = 65; x < sizeof(scratch[combination]); ++x) CHECK(scratch[combination][x] == 0xb7);
        }
        for (combination = 1; combination < 4; ++combination)
            for (y = 0; y < frames[0].height; ++y)
                for (x = 0; x < frames[0].width; ++x) {
                    CHECK(get_y(&frames[0], x, y) == get_y(&frames[combination], x, y));
                    CHECK(get_c(&frames[0], x, y, 0) == get_c(&frames[combination], x, y, 0));
                    CHECK(get_c(&frames[0], x, y, 1) == get_c(&frames[combination], x, y, 1));
                }
    }
    ++checks;
}

static void test_highlight_isolation_and_tint(void)
{
    uint8_t storage[3 * 128], before[3 * 128], scratch[128];
    se_ya_frame frame = descriptor(storage, sizeof(storage), 64, 3, 128);
    unsigned tint;
    uint32_t y, x;
    for (tint = 0; tint < 2; ++tint) {
        se_ya_params params = strong(SE_YA_FULL_RANGE, tint);
        for (y = 0; y < 3; ++y)
            for (x = 0; x < 64; x += 2u)
                set_pair(&frame, x, y, 32, 32, 128, 128);
        set_pair(&frame, 32, 1, 255, 32, 128, 128);
        memcpy(before, storage, sizeof(storage));
        CHECK(se_ya_apply(&frame, &params, scratch, sizeof(scratch)) == SE_YA_OK);
        CHECK(memcmp(storage, before, 128) == 0);
        CHECK(memcmp(storage + 256, before + 256, 128) == 0);
        CHECK(get_y(&frame, 28, 1) > 32 && get_y(&frame, 36, 1) > 32);
        CHECK(get_y(&frame, 32, 1) == 255);
        if (tint == 0) {
            CHECK(get_c(&frame, 28, 1, 0) > 128);
            CHECK(get_c(&frame, 28, 1, 1) < 128);
        } else {
            CHECK(get_c(&frame, 28, 1, 0) < 128);
            CHECK(get_c(&frame, 28, 1, 1) > 128);
        }
        for (x = 0; x < 64; ++x) CHECK(get_y(&frame, x, 1) >= (x == 32 ? 255u : 32u));
    }
    ++checks;
}

static void test_studio_headroom_and_tint_disabled(void)
{
    uint8_t storage[32], before[32], scratch[32];
    se_ya_frame frame = descriptor(storage, sizeof(storage), 16, 1, 32);
    se_ya_params params = strong(SE_YA_STUDIO_RANGE, 0);
    unsigned encoding;
    uint32_t x;
    frame.range = SE_YA_STUDIO_RANGE;
    for (encoding = 1; encoding <= 2; ++encoding) {
        frame.chroma = (se_ya_chroma)encoding;
        for (x = 0; x < 16; x += 2u) set_pair(&frame, x, 0, 32, 32, 128, 128);
        set_pair(&frame, 0, 0, 0, 15, 0, 255);
        set_pair(&frame, 2, 0, 236, 255, 15, 241);
        set_pair(&frame, 8, 0, 235, 32, 128, 128);
        memcpy(before, storage, sizeof(storage));
        CHECK(se_ya_apply(&frame, &params, scratch, sizeof(scratch)) == SE_YA_OK);
        CHECK(get_y(&frame, 0, 0) == 0 && get_y(&frame, 1, 0) == 15);
        CHECK(get_y(&frame, 2, 0) == 236 && get_y(&frame, 3, 0) == 255);
        CHECK(get_c(&frame, 0, 0, 0) == 0 && get_c(&frame, 0, 0, 1) == 255);
        CHECK(get_c(&frame, 2, 0, 0) == 15 && get_c(&frame, 2, 0, 1) == 241);
        for (x = 4; x < 16; ++x) CHECK(get_y(&frame, x, 0) >= 16 && get_y(&frame, x, 0) <= 235);
        /* Chroma disable is independent of luma streak processing. */
        memcpy(storage, before, sizeof(storage));
        params.tint_strength_q8 = 0;
        CHECK(se_ya_apply(&frame, &params, scratch, sizeof(scratch)) == SE_YA_OK);
        for (x = 0; x < 16; x += 2u) {
            CHECK(pair_at(&frame, x, 0)[1] == before[x * 2u + 1u]);
            CHECK(pair_at(&frame, x, 0)[3] == before[x * 2u + 3u]);
        }
        CHECK(get_y(&frame, 6, 0) > 32);
        params.tint_strength_q8 = 256;
    }
    ++checks;
}

static void expect_error(se_ya_frame frame, se_ya_params params, void *scratch,
                         size_t scratch_size, se_ya_result expected,
                         uint8_t *storage, size_t storage_size,
                         uint8_t *arena, size_t arena_size)
{
    uint8_t before_storage[512], before_arena[512];
    se_ya_result result;
    CHECK(storage_size <= sizeof(before_storage) && arena_size <= sizeof(before_arena));
    memcpy(before_storage, storage, storage_size);
    memcpy(before_arena, arena, arena_size);
    result = se_ya_apply(&frame, &params, scratch, scratch_size);
    CHECK(result != SE_YA_OK);
    if (expected != SE_YA_OK) CHECK(result == expected);
    CHECK(memcmp(before_storage, storage, storage_size) == 0);
    CHECK(memcmp(before_arena, arena, arena_size) == 0);
}

static void test_descriptor_rejection(void)
{
    uint8_t storage[256], arena[256];
    se_ya_frame good = descriptor(storage + 1, 240, 8, 3, 25), bad;
    se_ya_params params = strong(SE_YA_FULL_RANGE, 0), wrong;
    unsigned i;
    memset(storage, 0x59, sizeof(storage));
    memset(arena, 0x96, sizeof(arena));
#define REJECT_FRAME(change,code) do { bad = good; change; \
    expect_error(bad,params,arena+1,16,code,storage,sizeof(storage),arena,sizeof(arena)); } while(0)
    REJECT_FRAME(bad.data = NULL, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.width = 0, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.width = 1, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.width = 7, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.width = UINT32_MAX, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.width = SE_YA_MAX_WIDTH + 2u, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.height = 0, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.height = UINT32_MAX, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.height = SE_YA_MAX_HEIGHT + 1u, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.stride = 15, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.stride = 0, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.stride = (size_t)-1, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.bytes = 65, SE_YA_BAD_DESCRIPTOR); /* Need 2*25+16. */
    REJECT_FRAME(bad.bytes = (size_t)-1, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.layout = (se_ya_layout)0, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.range = (se_ya_range)7, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.chroma = (se_ya_chroma)0, SE_YA_BAD_DESCRIPTOR);
    REJECT_FRAME(bad.ownership = SE_YA_NOT_OWNED, SE_YA_BAD_OWNERSHIP);
    REJECT_FRAME(bad.ownership = (se_ya_ownership)9, SE_YA_BAD_OWNERSHIP);
    REJECT_FRAME(bad.data = (uint8_t *)(uintptr_t)(UINTPTR_MAX-7u), SE_YA_BAD_DESCRIPTOR);
#undef REJECT_FRAME
    expect_error(good, params, NULL, 16, SE_YA_SCRATCH_TOO_SMALL, storage, sizeof(storage), arena, sizeof(arena));
    expect_error(good, params, arena + 1, 15, SE_YA_SCRATCH_TOO_SMALL, storage, sizeof(storage), arena, sizeof(arena));
    expect_error(good, params, storage + 1, 16, SE_YA_OVERLAP, storage, sizeof(storage), arena, sizeof(arena));
    expect_error(good, params, storage + 20, 16, SE_YA_OVERLAP, storage, sizeof(storage), arena, sizeof(arena));
    expect_error(good, params, storage + 200, 16, SE_YA_OVERLAP, storage, sizeof(storage), arena, sizeof(arena));
    expect_error(good, params, arena, (size_t)-1, SE_YA_OK, storage, sizeof(storage), arena, sizeof(arena));
    expect_error(good, params, (void *)(uintptr_t)(UINTPTR_MAX-3u), 16, SE_YA_OK, storage, sizeof(storage), arena, sizeof(arena));
    CHECK(se_ya_apply(NULL, &params, arena, sizeof(arena)) != SE_YA_OK);
    CHECK(se_ya_apply(&good, NULL, arena, sizeof(arena)) != SE_YA_OK);
    for (i = 0; i < 11; ++i) {
        wrong = params;
        switch (i) {
        case 0: wrong.strength_q8 = 257; break;
        case 1: wrong.gain = 9; break;
        case 2: wrong.threshold = 256; break;
        case 3: wrong.short_decay_shift = 0; break;
        case 4: wrong.short_decay_shift = 9; break;
        case 5: wrong.long_decay_shift = 0; break;
        case 6: wrong.long_decay_shift = 9; break;
        case 7: wrong.short_decay_shift = 8; wrong.long_decay_shift = 1; break;
        case 8: wrong.tint_strength_q8 = 257; break;
        case 9: wrong.tint_cb = 256; break;
        default: wrong.tint_cr = 256; break;
        }
        expect_error(good, wrong, arena, sizeof(arena), SE_YA_BAD_PARAMS, storage, sizeof(storage), arena, sizeof(arena));
    }
    good.range = SE_YA_STUDIO_RANGE;
    params = strong(SE_YA_STUDIO_RANGE, 0);
    wrong = params; wrong.threshold = 15;
    expect_error(good, wrong, arena, sizeof(arena), SE_YA_BAD_PARAMS, storage, sizeof(storage), arena, sizeof(arena));
    wrong = params; wrong.threshold = 236;
    expect_error(good, wrong, arena, sizeof(arena), SE_YA_BAD_PARAMS, storage, sizeof(storage), arena, sizeof(arena));
    wrong = params; wrong.tint_cb = 15;
    expect_error(good, wrong, arena, sizeof(arena), SE_YA_BAD_PARAMS, storage, sizeof(storage), arena, sizeof(arena));
    wrong = params; wrong.tint_cr = 241;
    expect_error(good, wrong, arena, sizeof(arena), SE_YA_BAD_PARAMS, storage, sizeof(storage), arena, sizeof(arena));
    /* Invalid parameters are not hidden by overall bypass. */
    wrong = params; wrong.strength_q8 = 0; wrong.long_decay_shift = 9;
    expect_error(good, wrong, NULL, 0, SE_YA_BAD_PARAMS, storage, sizeof(storage), arena, sizeof(arena));
    ++checks;
}

static void test_small_large_and_deterministic(void)
{
    uint8_t small[8] = { 255,128,16,128,0x6a,0x6a,0x6a,0x6a }, scratch_small[8];
    se_ya_frame frame = descriptor(small, 4, 2, 1, 4);
    se_ya_params params = strong(SE_YA_FULL_RANGE, 1);
    uint8_t *large, *other, *scratch;
    size_t width = SE_YA_MAX_WIDTH, bytes = width * 2u;
    uint32_t x;
    CHECK(se_ya_apply(&frame, &params, scratch_small, 4) == SE_YA_OK);
    CHECK(small[0] == 255 && small[2] >= 16);
    CHECK(small[4] == 0x6a && small[7] == 0x6a);
    large = (uint8_t *)malloc(bytes + 2u);
    other = (uint8_t *)malloc(bytes + 2u);
    scratch = (uint8_t *)malloc(bytes + 2u);
    CHECK(large && other && scratch);
    memset(large, 0x6a, bytes + 2u);
    memset(scratch, 0xa6, bytes + 2u);
    frame = descriptor(large + 1, bytes, (uint32_t)width, 1, bytes);
    for (x = 0; x < width; x += 2u) set_pair(&frame, x, 0, 255, 255, 128, 128);
    memcpy(other, large, bytes + 2u);
    CHECK(se_ya_apply(&frame, &params, scratch + 1, bytes) == SE_YA_OK);
    frame.data = other + 1;
    CHECK(se_ya_apply(&frame, &params, scratch + 1, bytes) == SE_YA_OK);
    CHECK(memcmp(large, other, bytes + 2u) == 0);
    for (x = 0; x < width; ++x) CHECK(get_y(&frame, x, 0) == 255);
    CHECK(large[0] == 0x6a && large[bytes + 1u] == 0x6a);
    CHECK(scratch[0] == 0xa6 && scratch[bytes + 1u] == 0xa6);
    free(scratch); free(other); free(large);
    ++checks;
}

static void test_random_valid_descriptors(void)
{
    uint8_t one[1024], two[1024], before[1024], scratch[160];
    unsigned trial;
    for (trial = 0; trial < 300; ++trial) {
        uint32_t width = 2u * (1u + next_random() % 32u);
        uint32_t height = 1u + next_random() % 5u;
        size_t stride = (size_t)width * 2u + next_random() % 11u;
        size_t bytes = stride * height + 7u;
        size_t i;
        se_ya_frame frame = descriptor(one + 1, bytes, width, height, stride), other;
        se_ya_params params;
        frame.layout = next_random() & 1u ? SE_YA_YUYV : SE_YA_UYVY;
        frame.range = next_random() & 1u ? SE_YA_FULL_RANGE : SE_YA_STUDIO_RANGE;
        frame.chroma = next_random() & 1u ? SE_YA_CHROMA_UNSIGNED_128 : SE_YA_CHROMA_SIGNED_0;
        params = strong(frame.range, next_random() & 1u);
        CHECK(bytes + 2u <= sizeof(one));
        for (i = 0; i < sizeof(one); ++i) one[i] = (uint8_t)next_random();
        memcpy(two, one, sizeof(one));
        memcpy(before, one, sizeof(one));
        memset(scratch, 0x4d, sizeof(scratch));
        CHECK(se_ya_apply(&frame, &params, scratch + 1, width * 2u) == SE_YA_OK);
        padding_unchanged(before + 1, &frame);
        CHECK(one[0] == before[0] && one[bytes + 1u] == before[bytes + 1u]);
        CHECK(scratch[0] == 0x4d && scratch[width * 2u + 1u] == 0x4d);
        other = frame; other.data = two + 1;
        CHECK(se_ya_apply(&other, &params, scratch + 1, width * 2u) == SE_YA_OK);
        CHECK(memcmp(one, two, sizeof(one)) == 0);
    }
    ++checks;
}

static void test_metadata_aliases(void)
{
    union { se_ya_frame frame; uint8_t bytes[256]; } frame_alias;
    union { se_ya_params params; uint8_t bytes[256]; } params_alias;
    uint8_t control[256], pixels[256], scratch_one[64], scratch_two[64];
    se_ya_frame expected, actual;
    se_ya_params params = strong(SE_YA_FULL_RANGE, 0);
    size_t i;

    /* A descriptor living in its writable image must be snapshotted: the first
     * processed row can change its data/bytes/stride before the next row. */
    memset(&frame_alias, 0xff, sizeof(frame_alias));
    frame_alias.frame = descriptor(frame_alias.bytes, sizeof(frame_alias.bytes), 16, 3, 32);
    memcpy(control, frame_alias.bytes, sizeof(control));
    expected = frame_alias.frame; expected.data = control;
    CHECK(se_ya_apply(&expected, &params, scratch_one, sizeof(scratch_one)) == SE_YA_OK);
    CHECK(se_ya_apply(&frame_alias.frame, &params, scratch_two, sizeof(scratch_two)) == SE_YA_OK);
    CHECK(memcmp(frame_alias.bytes, control, sizeof(control)) == 0);

    /* A scratch row may likewise overwrite the descriptor itself. */
    for (i = 0; i < sizeof(pixels); ++i) pixels[i] = (uint8_t)next_random();
    memcpy(control, pixels, sizeof(control));
    frame_alias.frame = descriptor(pixels, sizeof(pixels), 16, 3, 32);
    expected = frame_alias.frame; expected.data = control;
    CHECK(se_ya_apply(&expected, &params, scratch_one, sizeof(scratch_one)) == SE_YA_OK);
    CHECK(se_ya_apply(&frame_alias.frame, &params, frame_alias.bytes, sizeof(frame_alias.bytes)) == SE_YA_OK);
    CHECK(memcmp(pixels, control, sizeof(control)) == 0);

    /* Parameters embedded in an image are also inputs, not live mutable state. */
    memset(&params_alias, 0xff, sizeof(params_alias));
    params_alias.params = params;
    memcpy(control, params_alias.bytes, sizeof(control));
    actual = descriptor(params_alias.bytes, sizeof(params_alias.bytes), 32, 2, 64);
    expected = actual; expected.data = control;
    CHECK(se_ya_apply(&expected, &params, scratch_one, sizeof(scratch_one)) == SE_YA_OK);
    CHECK(se_ya_apply(&actual, &params_alias.params, scratch_two, sizeof(scratch_two)) == SE_YA_OK);
    CHECK(memcmp(params_alias.bytes, control, sizeof(control)) == 0);

    /* Forward highlight storage destroys parameters held in scratch before the
     * backward rendering scan, so this exercises the complete settings copy. */
    memset(pixels, 0xff, sizeof(pixels));
    memcpy(control, pixels, sizeof(control));
    params_alias.params = params;
    actual = descriptor(pixels, sizeof(pixels), 32, 2, 64);
    expected = actual; expected.data = control;
    CHECK(se_ya_apply(&expected, &params, scratch_one, sizeof(scratch_one)) == SE_YA_OK);
    CHECK(se_ya_apply(&actual, &params_alias.params, params_alias.bytes, sizeof(params_alias.bytes)) == SE_YA_OK);
    CHECK(memcmp(pixels, control, sizeof(control)) == 0);
    ++checks;
}

static void test_minimal_span_and_inaccessible_padding(void)
{
    uint8_t *pixels = (uint8_t *)malloc(64);
    uint8_t scratch[16];
    se_ya_frame frame;
    se_ya_params params = strong(SE_YA_FULL_RANGE, 0);
    uint32_t x, y;
    CHECK(pixels != NULL);
    memset(pixels, 0xc7, 64);
    frame = descriptor(pixels, 64, 8, 3, 24); /* Last row has no padding allocation. */
    for (y = 0; y < 3; ++y)
        for (x = 0; x < 8; x += 2u) set_pair(&frame, x, y, 32, 255, 128, 128);
#if defined(__SANITIZE_ADDRESS__)
    __asan_poison_memory_region(pixels + 16, 8);
    __asan_poison_memory_region(pixels + 40, 8);
#endif
    CHECK(se_ya_apply(&frame, &params, scratch, sizeof(scratch)) == SE_YA_OK);
#if defined(__SANITIZE_ADDRESS__)
    __asan_unpoison_memory_region(pixels + 16, 8);
    __asan_unpoison_memory_region(pixels + 40, 8);
#endif
    for (x = 16; x < 24; ++x) CHECK(pixels[x] == 0xc7);
    for (x = 40; x < 48; ++x) CHECK(pixels[x] == 0xc7);
    free(pixels);
    ++checks;
}

static void test_maximum_arithmetic_and_height(void)
{
    static const uint8_t shifts[3][2] = {{1,1},{1,8},{8,8}};
    uint8_t pixels[128], duplicate[128], scratch[128];
    unsigned range, encoding, shape, shift;
    uint32_t x;
    for (range = 1; range <= 2; ++range)
        for (encoding = 1; encoding <= 2; ++encoding)
            for (shape = 0; shape < 2; ++shape)
                for (shift = 0; shift < 3; ++shift) {
                    unsigned ymin = range == 2 ? 16u : 0u;
                    unsigned ymax = range == 2 ? 235u : 255u;
                    unsigned cmax = range == 2 ? 240u : 255u;
                    se_ya_frame frame = descriptor(pixels, sizeof(pixels), 64, 1, 128), other;
                    se_ya_params params = strong((se_ya_range)range, 0);
                    frame.range = (se_ya_range)range;
                    frame.chroma = (se_ya_chroma)encoding;
                    params.threshold = (uint16_t)ymin;
                    params.short_decay_shift = shifts[shift][0];
                    params.long_decay_shift = shifts[shift][1];
                    params.tint_cb = (uint16_t)ymin;
                    params.tint_cr = (uint16_t)cmax;
                    for (x = 0; x < 64; x += 2u)
                        set_pair(&frame, x, 0, shape ? ymax : ymin,
                                 shape ? ymax : ymin, 128, 128);
                    if (!shape) set_pair(&frame, 32, 0, ymax, ymin, 128, 128);
                    memcpy(duplicate, pixels, sizeof(pixels));
                    CHECK(se_ya_apply(&frame, &params, scratch, sizeof(scratch)) == SE_YA_OK);
                    for (x = 0; x < 64; ++x) {
                        CHECK(get_y(&frame, x, 0) >= ymin && get_y(&frame, x, 0) <= ymax);
                        CHECK(get_c(&frame, x, 0, 0) >= ymin && get_c(&frame, x, 0, 0) <= cmax);
                        CHECK(get_c(&frame, x, 0, 1) >= ymin && get_c(&frame, x, 0, 1) <= cmax);
                        if (shape) CHECK(get_y(&frame, x, 0) == ymax);
                    }
                    if (!shape) {
                        CHECK(get_y(&frame, 28, 0) > ymin && get_y(&frame, 36, 0) > ymin);
                        CHECK(get_y(&frame, 32, 0) == ymax);
                    }
                    other = frame; other.data = duplicate;
                    CHECK(se_ya_apply(&other, &params, scratch, sizeof(scratch)) == SE_YA_OK);
                    CHECK(memcmp(pixels, duplicate, sizeof(pixels)) == 0);
                }
    {
        const size_t stride = 8, allocation = (size_t)SE_YA_MAX_HEIGHT * stride;
        uint8_t *tall = (uint8_t *)malloc(allocation);
        uint8_t scratch_small[4];
        se_ya_frame frame;
        se_ya_params params = strong(SE_YA_STUDIO_RANGE, 1);
        uint32_t y;
        size_t index;
        CHECK(tall != NULL);
        memset(tall, 0xb3, allocation);
        frame = descriptor(tall, allocation - 4u, 2, SE_YA_MAX_HEIGHT, stride);
        frame.range = SE_YA_STUDIO_RANGE;
        params.threshold = 16;
        params.short_decay_shift = params.long_decay_shift = 8;
        for (y = 0; y < frame.height; ++y) set_pair(&frame, 0, y, 16, 235, 128, 128);
        CHECK(se_ya_apply(&frame, &params, scratch_small, sizeof(scratch_small)) == SE_YA_OK);
        for (y = 0; y < frame.height; ++y) {
            CHECK(get_y(&frame, 0, y) >= 16 && get_y(&frame, 0, y) <= 235);
            CHECK(get_y(&frame, 1, y) == 235);
        }
        for (index = 0; index < allocation; ++index)
            if (index % stride >= 4u) CHECK(tall[index] == 0xb3);
        free(tall);
    }
    ++checks;
}

int main(void)
{
    test_presets_and_sizing();
    test_bypass();
    test_layout_and_representation_equivalence();
    test_highlight_isolation_and_tint();
    test_studio_headroom_and_tint_disabled();
    test_descriptor_rejection();
    test_small_large_and_deterministic();
    test_random_valid_descriptors();
    test_metadata_aliases();
    test_minimal_span_and_inaccessible_padding();
    test_maximum_arithmetic_and_height();
    printf("YUV anamorphic: %u contract groups and 300 deterministic random frames passed\n", checks);
    return 0;
}
