/* Experimental, camera-independent packed-YUV streak prototype. GPL-2.0-only. */
#include "yuv_anamorphic.h"

typedef struct {
    unsigned y_min, y_max, c_min, c_max;
} ya_limits;

static int range_limits(se_ya_range range, ya_limits *limits)
{
    if (range == SE_YA_FULL_RANGE) {
        limits->y_min = limits->c_min = 0u;
        limits->y_max = limits->c_max = 255u;
        return 1;
    }
    if (range == SE_YA_STUDIO_RANGE) {
        limits->y_min = limits->c_min = 16u;
        limits->y_max = 235u;
        limits->c_max = 240u;
        return 1;
    }
    return 0;
}

se_ya_result se_ya_preset(se_ya_params *params, se_ya_range range,
                        unsigned preset, unsigned tint)
{
    static const uint16_t strength[4] = {0u, 96u, 176u, 256u};
    static const uint16_t gain[4] = {0u, 3u, 5u, 8u};
    static const uint16_t color[4] = {0u, 96u, 144u, 192u};
    se_ya_params value;
    ya_limits limits;
    if (!params || !range_limits(range, &limits) || preset > 3u || tint > 1u)
        return SE_YA_BAD_PARAMS;
    value.strength_q8 = strength[preset];
    value.gain = gain[preset];
    value.threshold = (uint16_t)(range == SE_YA_FULL_RANGE ? 220u : 205u);
    value.short_decay_shift = (uint8_t)(preset == 3u ? 4u : 3u);
    value.long_decay_shift = (uint8_t)(preset == 3u ? 8u : 6u + (preset == 2u));
    value.tint_strength_q8 = color[preset];
    if (range == SE_YA_FULL_RANGE) {
        value.tint_cb = (uint16_t)(tint ? 88u : 196u);
        value.tint_cr = (uint16_t)(tint ? 172u : 112u);
    } else {
        value.tint_cb = (uint16_t)(tint ? 93u : 188u);
        value.tint_cr = (uint16_t)(tint ? 167u : 114u);
    }
    *params = value;
    return SE_YA_OK;
}

size_t se_ya_scratch_bytes(uint32_t width)
{
    if (width < 2u || width > SE_YA_MAX_WIDTH || (width & 1u)) return 0u;
    return (size_t)width * 2u;
}

static int interval(const void *pointer, size_t bytes, uintptr_t *start,
                    uintptr_t *end)
{
    uintptr_t address = (uintptr_t)pointer;
    if (!pointer || !bytes || bytes > (size_t)PTRDIFF_MAX ||
        bytes > UINTPTR_MAX - address) return 0;
    *start = address;
    *end = address + bytes;
    return 1;
}

static int params_valid(const se_ya_params *params, const ya_limits *limits)
{
    return params->strength_q8 <= 256u && params->gain <= 8u &&
        params->threshold >= limits->y_min && params->threshold <= limits->y_max &&
        params->short_decay_shift >= 1u && params->short_decay_shift <= 8u &&
        params->long_decay_shift >= params->short_decay_shift &&
        params->long_decay_shift <= 8u && params->tint_strength_q8 <= 256u &&
        params->tint_cb >= limits->c_min && params->tint_cb <= limits->c_max &&
        params->tint_cr >= limits->c_min && params->tint_cr <= limits->c_max;
}

/* A ceil-rounded loss makes even a sub-code-value Q8 tail eventually vanish.
 * Max rather than addition prevents adjacent highlights accumulating overflow.
 * All values are positive; no implementation-defined negative right shifts. */
static unsigned tail_step(unsigned tail, unsigned highlight, unsigned shift)
{
    unsigned loss = (tail + ((1u << shift) - 1u)) >> shift;
    tail -= loss;
    return highlight > tail ? highlight : tail;
}

static unsigned highlight(unsigned y, unsigned threshold, const ya_limits *limits)
{
    if (y > limits->y_max) y = limits->y_max;
    if (y < limits->y_min) y = limits->y_min;
    return y > threshold ? (y - threshold) << 8 : 0u;
}

static void store_q8(uint8_t *scratch, uint32_t x, unsigned value)
{
    size_t offset = (size_t)x * 2u;
    scratch[offset] = (uint8_t)value;
    scratch[offset + 1u] = (uint8_t)(value >> 8);
}

static unsigned load_q8(const uint8_t *scratch, uint32_t x)
{
    size_t offset = (size_t)x * 2u;
    return (unsigned)scratch[offset] | ((unsigned)scratch[offset + 1u] << 8);
}

static unsigned combined(unsigned fast, unsigned slow)
{
    return (fast * 3u + slow + 2u) >> 2;
}

static unsigned energy(unsigned left, unsigned right, const se_ya_params *params)
{
    unsigned value = left > right ? left : right;
    /* <=65280*8*256+32768, well below UINT32_MAX on ARM946E-S. */
    value = (value * params->gain * params->strength_q8 + 32768u) >> 16;
    return value > 255u ? 255u : value;
}

static unsigned lift_luma(unsigned value, unsigned amount, const ya_limits *limits)
{
    if (value < limits->y_min || value > limits->y_max) return value;
    return value + (((limits->y_max - value) * amount + 128u) >> 8);
}

static uint8_t tint_chroma(uint8_t stored, unsigned target, unsigned weight,
                           unsigned storage_xor, const ya_limits *limits)
{
    unsigned original = (unsigned)stored ^ storage_xor;
    unsigned difference, step, result;
    if (!weight || original < limits->c_min || original > limits->c_max)
        return stored;
    if (target >= original) {
        difference = target - original;
        step = (difference * weight + 128u) >> 8;
        result = original + step;
    } else {
        difference = original - target;
        step = (difference * weight + 128u) >> 8;
        result = original - step;
    }
    return (uint8_t)(result ^ storage_xor);
}

static void apply_row(uint8_t *row, uint8_t *scratch, uint32_t width,
                      unsigned y_offset, unsigned storage_xor,
                      const se_ya_params *params, const ya_limits *limits)
{
    uint32_t pair;
    unsigned fast = 0u, slow = 0u;
    unsigned cb_offset = y_offset ? 0u : 1u;
    unsigned cr_offset = cb_offset + 2u;

    /* Finish the entire left-to-right scan before changing any source byte. */
    for (pair = 0u; pair < width / 2u; ++pair) {
        size_t offset = (size_t)pair * 4u + y_offset;
        uint32_t x = pair * 2u;
        unsigned h = highlight(row[offset], params->threshold, limits);
        fast = tail_step(fast, h, params->short_decay_shift);
        slow = tail_step(slow, h, params->long_decay_shift);
        store_q8(scratch, x, combined(fast, slow));
        h = highlight(row[offset + 2u], params->threshold, limits);
        fast = tail_step(fast, h, params->short_decay_shift);
        slow = tail_step(slow, h, params->long_decay_shift);
        store_q8(scratch, x + 1u, combined(fast, slow));
    }

    fast = slow = 0u;
    for (pair = width / 2u; pair != 0u; --pair) {
        size_t offset = (size_t)(pair - 1u) * 4u;
        uint32_t x = (pair - 1u) * 2u;
        unsigned y0 = row[offset + y_offset];
        unsigned y1 = row[offset + y_offset + 2u];
        unsigned h = highlight(y1, params->threshold, limits);
        unsigned e0, e1, weight;
        fast = tail_step(fast, h, params->short_decay_shift);
        slow = tail_step(slow, h, params->long_decay_shift);
        e1 = energy(load_q8(scratch, x + 1u), combined(fast, slow), params);
        h = highlight(y0, params->threshold, limits);
        fast = tail_step(fast, h, params->short_decay_shift);
        slow = tail_step(slow, h, params->long_decay_shift);
        e0 = energy(load_q8(scratch, x), combined(fast, slow), params);

        row[offset + y_offset] = (uint8_t)lift_luma(y0, e0, limits);
        row[offset + y_offset + 2u] = (uint8_t)lift_luma(y1, e1, limits);
        if (y0 < limits->y_min || y0 > limits->y_max) e0 = 0u;
        if (y1 < limits->y_min || y1 > limits->y_max) e1 = 0u;
        weight = ((((e0 + e1 + 1u) >> 1) * params->tint_strength_q8) + 128u) >> 8;
        row[offset + cb_offset] = tint_chroma(row[offset + cb_offset],
            params->tint_cb, weight, storage_xor, limits);
        row[offset + cr_offset] = tint_chroma(row[offset + cr_offset],
            params->tint_cr, weight, storage_xor, limits);
    }
}

se_ya_result se_ya_apply(const se_ya_frame *frame, const se_ya_params *params,
                        void *scratch, size_t scratch_bytes)
{
    se_ya_frame value;
    se_ya_params settings;
    ya_limits limits;
    uintptr_t frame_start, frame_end, scratch_start, scratch_end;
    size_t need, row_bytes, rows_before_last;
    uint32_t y;
    unsigned y_offset, storage_xor;
    if (!frame) return SE_YA_BAD_DESCRIPTOR;
    if (!params) return SE_YA_BAD_PARAMS;
    /* Metadata can alias either caller allocation: take complete snapshots
     * before validating or modifying the frame/scratch they might live in. */
    value = *frame;
    settings = *params;
    need = se_ya_scratch_bytes(value.width);
    if (!need || !value.height || value.height > SE_YA_MAX_HEIGHT ||
        (value.layout != SE_YA_YUYV && value.layout != SE_YA_UYVY) ||
        (value.chroma != SE_YA_CHROMA_UNSIGNED_128 &&
         value.chroma != SE_YA_CHROMA_SIGNED_0) ||
        !range_limits(value.range, &limits)) return SE_YA_BAD_DESCRIPTOR;
    row_bytes = (size_t)value.width * 2u;
    if (value.stride < row_bytes || value.stride > (size_t)PTRDIFF_MAX ||
        !interval(value.data, value.bytes, &frame_start, &frame_end))
        return SE_YA_BAD_DESCRIPTOR;
    rows_before_last = (size_t)value.height - 1u;
    if (rows_before_last > (SIZE_MAX - row_bytes) / value.stride ||
        rows_before_last * value.stride + row_bytes > value.bytes)
        return SE_YA_BAD_DESCRIPTOR;
    if (value.ownership != SE_YA_EXCLUSIVE_CPU) return SE_YA_BAD_OWNERSHIP;
    if (!params_valid(&settings, &limits)) return SE_YA_BAD_PARAMS;
    if (!settings.strength_q8 || !settings.gain || settings.threshold == limits.y_max)
        return SE_YA_OK;
    if (!scratch || scratch_bytes < need) return SE_YA_SCRATCH_TOO_SMALL;
    if (!interval(scratch, scratch_bytes, &scratch_start, &scratch_end))
        return SE_YA_BAD_DESCRIPTOR;
    if (scratch_start < frame_end && frame_start < scratch_end)
        return SE_YA_OVERLAP;

    y_offset = value.layout == SE_YA_UYVY ? 1u : 0u;
    storage_xor = value.chroma == SE_YA_CHROMA_SIGNED_0 ? 128u : 0u;
    for (y = 0u; y < value.height; ++y) {
        uint8_t *row = value.data + (size_t)y * value.stride;
        apply_row(row, (uint8_t *)scratch, value.width, y_offset, storage_xor,
                  &settings, &limits);
    }
    return SE_YA_OK;
}
