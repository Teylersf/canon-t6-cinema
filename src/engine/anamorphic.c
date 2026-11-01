#include "anamorphic.h"
#include "se_libc.h"

struct se_anamorphic_run {
    se_anamorphic p;
    int width, height, radius, ring, spatial;
    int streak, bloom, vignette, chromatic;
    int input, output, step, ended, failed;
    uint16_t *mask, *flares, *blooms;
    uint32_t *bloom_sum;
    uint8_t *source, *out;
};

static int bounded(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static se_anamorphic normalized(const se_anamorphic *p)
{
    se_anamorphic q = *p;
    q.strength = bounded(q.strength, 0, 100);
    q.streak = bounded(q.streak, 0, 100);
    q.bloom = bounded(q.bloom, 0, 100);
    q.vignette = bounded(q.vignette, 0, 100);
    q.chromatic = bounded(q.chromatic, 0, 100);
    q.threshold = bounded(q.threshold, 0, 255);
    q.streak_radius = bounded(q.streak_radius, 1, SE_ANAMORPHIC_MAX_RADIUS);
    q.vertical_radius = bounded(q.vertical_radius, 0, SE_ANAMORPHIC_MAX_VERTICAL);
    if (!q.strength || !q.bloom || q.threshold == 255)
        q.vertical_radius = 0;
    return q;
}

void se_anamorphic_preset(se_anamorphic *p, int preset, int tint, int width)
{
    static const int values[4][5] = {
        {0, 0, 0, 0, 0}, {35, 40, 12, 12, 5},
        {65, 60, 25, 18, 10}, {100, 80, 40, 30, 18}
    };
    int k = bounded(preset, 0, 3);
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->strength = values[k][0]; p->streak = values[k][1];
    p->bloom = values[k][2]; p->vignette = values[k][3];
    p->chromatic = values[k][4]; p->threshold = 220;
    p->streak_radius = bounded(bounded(width, 1, SE_ANAMORPHIC_MAX_WIDTH) / 14,
                               8, SE_ANAMORPHIC_MAX_RADIUS);
    p->vertical_radius = k == 3 ? 3 : 2;
    p->tint[0] = tint == 1 ? 255 : 85;
    p->tint[1] = tint == 1 ? 174 : 160;
    p->tint[2] = tint == 1 ? 75 : 255;
}

void se_anamorphic_default(se_anamorphic *p)
{
    se_anamorphic_preset(p, 2, 0, 1800);
}

static size_t padded(size_t n) { return (n + 7u) & ~(size_t)7u; }

static int active(const se_anamorphic *p)
{
    return p->strength && (p->streak || p->bloom || p->vignette || p->chromatic);
}

static int spatial(const se_anamorphic *p)
{
    return p->strength && p->threshold < 255 && (p->streak || p->bloom);
}

size_t se_anamorphic_bytes(const se_anamorphic *p, int width)
{
    se_anamorphic q;
    size_t w, n;
    if (!p || width < 1 || width > SE_ANAMORPHIC_MAX_WIDTH) return 0;
    q = normalized(p); w = (size_t)width;
    n = padded(sizeof(se_anamorphic_run));
    if (active(&q)) n += padded(w * 3u);
    if (spatial(&q)) {
        size_t rows = (size_t)(q.vertical_radius * 2 + 1);
        n += padded(w * 2u);                          /* highlight mask */
        if (q.streak) n += padded(w * rows * 2u);      /* flare delay */
        if (q.bloom) {
            n += padded(w * rows * 2u);              /* bloom delay */
            n += padded(w * 4u);                     /* vertical sums */
        }
        n += padded(w * (size_t)(q.vertical_radius + 1) * 3u);
    }
    return n + 7u;
}

size_t se_anamorphic_fit(se_anamorphic *p, int width, size_t budget)
{
    size_t n;
    if (!p || width < 1 || width > SE_ANAMORPHIC_MAX_WIDTH) return 0;
    *p = normalized(p);
    for (;;) {
        n = se_anamorphic_bytes(p, width);
        if (n <= budget) return n;
        if (!p->vertical_radius) return 0;
        p->vertical_radius--;
    }
}

static void *take(uint8_t **cursor, size_t n)
{
    void *p = *cursor;
    *cursor += padded(n);
    return p;
}

se_err se_anamorphic_begin(se_anamorphic_run **run, const se_anamorphic *p,
                          int width, int height, se_arena *ar)
{
    size_t cost;
    uint8_t *mem, *cursor;
    se_anamorphic_run *r;
    se_anamorphic q;
    if (!run) return SE_E_CORRUPT;
    *run = NULL;
    cost = se_anamorphic_bytes(p, width);
    if (!cost || height < 1 || height > SE_ANAMORPHIC_MAX_HEIGHT ||
        !ar || !ar->base || ar->used > ar->size ||
        ar->used > (size_t)-1 - 7u) return SE_E_CORRUPT;
    q = normalized(p);
    /* Align the absolute pointer too: se_arena aligns offsets, while a
     * caller may deliberately supply an unaligned byte buffer. */
    mem = (uint8_t *)se_arena_alloc(ar, cost, 1);
    if (!mem) return SE_E_NOMEM;
    mem = (uint8_t *)(((uintptr_t)mem + 7u) & ~(uintptr_t)7u);
    memset(mem, 0, cost - 7u);
    cursor = mem;
    r = (se_anamorphic_run *)take(&cursor, sizeof(*r));
    r->p = q; r->width = width; r->height = height;
    r->radius = q.vertical_radius; r->ring = q.vertical_radius * 2 + 1;
    r->streak = q.streak * q.strength / 100;
    r->bloom = q.bloom * q.strength / 100;
    r->vignette = q.vignette * q.strength / 100;
    r->chromatic = q.chromatic * q.strength / 100;
    r->spatial = spatial(&q);
    if (active(&q)) r->out = (uint8_t *)take(&cursor, (size_t)width * 3u);
    if (r->spatial) {
        size_t pixels = (size_t)width * (size_t)r->ring;
        r->mask = (uint16_t *)take(&cursor, (size_t)width * 2u);
        if (q.streak) r->flares = (uint16_t *)take(&cursor, pixels * 2u);
        if (q.bloom) {
            r->blooms = (uint16_t *)take(&cursor, pixels * 2u);
            r->bloom_sum = (uint32_t *)take(&cursor, (size_t)width * 4u);
        }
        r->source = (uint8_t *)take(&cursor,
                       (size_t)width * (size_t)(r->radius + 1) * 3u);
    }
    *run = r;
    return SE_OK;
}

/* Zero-padded horizontal box filter. Q8 retains the faint long flare tail;
 * three radii cost O(width), independent of their reach. Accumulation is
 * saturated rather than allowed to wrap in a broad bright region. */
static void horizontal(const uint16_t *mask, uint16_t *dst, int width,
                       int radius, int weight, int add)
{
    uint32_t sum = 0;
    int x, k;
    for (k = 0; k <= radius && k < width; k++) sum += mask[k];
    for (x = 0; x < width; x++) {
        uint32_t v = ((sum << 8) / (unsigned)(radius * 2 + 1)) * (unsigned)weight;
        if (add) v += dst[x];
        dst[x] = (uint16_t)(v > 65535u ? 65535u : v);
        if (x - radius >= 0) sum -= mask[x - radius];
        if (x + radius + 1 < width) sum += mask[x + radius + 1];
    }
}

static int sample(const uint8_t *row, int width, int xq, int c)
{
    int x, f;
    xq = bounded(xq, 0, (width - 1) * 256);
    x = xq >> 8; f = xq & 255;
    if (!f || x + 1 == width) return row[x * 3 + c];
    return (row[x * 3 + c] * (256 - f) + row[(x + 1) * 3 + c] * f + 128) >> 8;
}

static se_err output_row(se_anamorphic_run *r, const uint8_t *src,
                         int y, se_anamorphic_emit emit, void *ctx)
{
    int x, c, ny = r->height == 1 ? 0 :
        (2 * y - (r->height - 1)) * 256 / (r->height - 1);
    const uint16_t *flare = r->flares ?
        r->flares + (size_t)(y % r->ring) * (size_t)r->width : NULL;
    if (!active(&r->p)) {
        if (emit(ctx, src, r->width)) return r->failed = SE_E_IO;
        r->output++;
        return SE_OK;
    }
    for (x = 0; x < r->width; x++) {
        int nx = r->width == 1 ? 0 :
            (2 * x - (r->width - 1)) * 256 / (r->width - 1);
        int offset = nx * 2 * r->chromatic / 100;
        int rgb[3], room, dark = 0;
        uint32_t fe = flare ? (uint32_t)flare[x] * (unsigned)r->streak / 100u : 0;
        uint32_t be = r->bloom_sum ?
            (r->bloom_sum[x] / (unsigned)r->ring) * (unsigned)r->bloom / 100u : 0;
        rgb[0] = sample(src, r->width, x * 256 + offset, 0);
        rgb[1] = src[x * 3 + 1];
        rgb[2] = sample(src, r->width, x * 256 - offset, 2);
        room = 255 - ((rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8);
        if (r->vignette) {
            unsigned edge = (unsigned)(nx * nx + ny * ny) >> 9;
            dark = (int)((((edge * edge * (unsigned)r->vignette) / 100u)
                         * 180u + 32768u) >> 16);
        }
        for (c = 0; c < 3; c++) {
            unsigned tint = r->p.tint[c], pale = (tint + 510u) / 3u;
            uint32_t energy = (fe * tint + 127u) / 255u + (be * pale + 127u) / 255u;
            int v = rgb[c] + (int)(((energy * (unsigned)room + 127u) / 255u + 128u) >> 8);
            v = bounded(v, 0, 255);
            r->out[x * 3 + c] = (uint8_t)((v * (256 - dark) + 128) >> 8);
        }
    }
    if (emit(ctx, r->out, r->width)) return r->failed = SE_E_IO;
    r->output++;
    return SE_OK;
}

static se_err step_row(se_anamorphic_run *r, const uint8_t *src,
                       se_anamorphic_emit emit, void *ctx)
{
    int x, j = r->step, w = r->width;
    uint16_t *f = r->flares ? r->flares + (size_t)(j % r->ring) * (size_t)w : NULL;
    uint16_t *b = r->blooms ? r->blooms + (size_t)(j % r->ring) * (size_t)w : NULL;
    if (b) for (x = 0; x < w; x++) r->bloom_sum[x] -= b[x];
    if (src) {
        memcpy(r->source + (size_t)(j % (r->radius + 1)) * (size_t)w * 3u,
               src, (size_t)w * 3u);
        for (x = 0; x < w; x++) {
            int v = SE_MAX(src[x * 3], SE_MAX(src[x * 3 + 1], src[x * 3 + 2]));
            r->mask[x] = (uint16_t)(v > r->p.threshold ?
                (v - r->p.threshold) * 255 / (255 - r->p.threshold) : 0);
        }
        if (f) {
            horizontal(r->mask, f, w, SE_MAX(1, r->p.streak_radius / 16), 2, 0);
            horizontal(r->mask, f, w, SE_MAX(1, r->p.streak_radius / 4), 3, 1);
            horizontal(r->mask, f, w, r->p.streak_radius, 5, 1);
        }
        if (b) horizontal(r->mask, b, w,
                SE_MAX(1, SE_MAX(r->p.streak_radius / 8, r->radius * 2)), 1, 0);
    } else {
        if (f) memset(f, 0, (size_t)w * 2u);
        if (b) memset(b, 0, (size_t)w * 2u);
    }
    if (b) for (x = 0; x < w; x++) r->bloom_sum[x] += b[x];
    r->step++;
    if (j >= r->radius) {
        int y = j - r->radius;
        const uint8_t *original = r->source +
            (size_t)(y % (r->radius + 1)) * (size_t)w * 3u;
        return output_row(r, original, y, emit, ctx);
    }
    return SE_OK;
}

se_err se_anamorphic_rows(se_anamorphic_run *r, const uint8_t *rgb,
                         int nrows, se_anamorphic_emit emit, void *ctx)
{
    int i;
    if (!r || !emit) return SE_E_CORRUPT;
    if (r->failed) return r->failed;
    if (r->ended || nrows < 0 || nrows > r->height - r->input ||
        (nrows && !rgb)) return SE_E_CORRUPT;
    for (i = 0; i < nrows; i++) {
        se_err e;
        const uint8_t *row = rgb + (size_t)i * (size_t)r->width * 3u;
        if (r->spatial) e = step_row(r, row, emit, ctx);
        else e = output_row(r, row, r->input, emit, ctx);
        r->input++;
        if (e) return e;
    }
    return SE_OK;
}

se_err se_anamorphic_end(se_anamorphic_run *r,
                        se_anamorphic_emit emit, void *ctx)
{
    int i;
    if (!r || !emit) return SE_E_CORRUPT;
    if (r->failed) return r->failed;
    if (r->ended) return SE_E_CORRUPT;
    if (r->input != r->height) return SE_E_TRUNCATED;
    if (r->spatial) for (i = 0; i < r->radius; i++) {
        se_err e = step_row(r, NULL, emit, ctx);
        if (e) return e;
    }
    r->ended = 1;
    return r->output == r->height ? SE_OK : SE_E_CORRUPT;
}
