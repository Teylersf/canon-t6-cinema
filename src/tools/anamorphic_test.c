/* Host-only behavioral and bounds checks. GPL v2.
 * cc -std=c99 -DSE_HOSTED -Iengine tools/anamorphic_test.c
 *    engine/anamorphic.c engine/se.c -o /tmp/anamorphic-test
 */
#include "anamorphic.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); \
} } while (0)

typedef struct {
    uint8_t *data;
    int width, height, rows, fail_after;
} sink;

static int collect(void *ctx, const uint8_t *row, int width)
{
    sink *s = (sink *)ctx;
    CHECK(width == s->width && s->rows < s->height);
    if (s->fail_after >= 0 && s->rows == s->fail_after) return 1;
    memcpy(s->data + (size_t)s->rows * (size_t)width * 3u, row, (size_t)width * 3u);
    s->rows++;
    return 0;
}

static void render(const se_anamorphic *p, int w, int h,
                   const uint8_t *input, uint8_t *output, int batch)
{
    size_t cost = se_anamorphic_bytes(p, w), i;
    uint8_t *memory = (uint8_t *)malloc(cost + 18u);
    se_arena ar;
    se_anamorphic_run *run = NULL;
    sink s = {output, w, h, 0, -1};
    int y;
    CHECK(memory && cost);
    memset(memory, 0xa7, cost + 18u);
    /* Both the base and starting offset are intentionally unaligned. */
    se_arena_init(&ar, memory + 1, cost + 3u);
    ar.used = 3;
    CHECK(se_anamorphic_begin(&run, p, w, h, &ar) == SE_OK);
    CHECK(ar.used <= ar.size && ar.peak <= ar.size);
    for (y = 0; y < h;) {
        int n = h - y < batch ? h - y : batch;
        CHECK(se_anamorphic_rows(run, input + (size_t)y * (size_t)w * 3u,
                                  n, collect, &s) == SE_OK);
        y += n;
    }
    CHECK(se_anamorphic_end(run, collect, &s) == SE_OK);
    CHECK(s.rows == h);
    CHECK(memory[0] == 0xa7);
    for (i = cost + 4u; i < cost + 18u; i++) CHECK(memory[i] == 0xa7);
    CHECK(se_anamorphic_end(run, collect, &s) == SE_E_CORRUPT);
    free(memory);
}

static void neutral_test(void)
{
    uint8_t input[31 * 9 * 3], output[sizeof input];
    se_anamorphic p;
    size_t i;
    for (i = 0; i < sizeof input; i++) input[i] = (uint8_t)(i * 73u + i / 13u);
    se_anamorphic_preset(&p, 3, 0, 31);
    p.strength = 0;
    render(&p, 31, 9, input, output, 1);
    CHECK(!memcmp(input, output, sizeof input));
    p.strength = 100; p.streak = p.bloom = p.vignette = p.chromatic = 0;
    render(&p, 31, 9, input, output, 9);
    CHECK(!memcmp(input, output, sizeof input));
    p.streak = p.bloom = 100; p.threshold = 255;
    render(&p, 31, 9, input, output, 2);
    CHECK(!memcmp(input, output, sizeof input));
}

static void highlight_test(void)
{
    enum {W = 65, H = 7};
    uint8_t input[W * H * 3], output[sizeof input];
    se_anamorphic p;
    size_t center = (3u * W + 32u) * 3u;
    memset(input, 20, sizeof input);
    input[center] = input[center + 1] = input[center + 2] = 255;
    se_anamorphic_preset(&p, 0, 0, W);
    p.strength = p.streak = 100; p.streak_radius = 12;
    p.threshold = 200;
    render(&p, W, H, input, output, 1);
    CHECK(output[center + 8u * 3u + 2u] > 20);
    CHECK(output[center + 8u * 3u + 2u] > output[center + 8u * 3u]);
    CHECK(!memcmp(input, output, 3u * W * 3u));
    CHECK(!memcmp(input + 4u * W * 3u, output + 4u * W * 3u, 3u * W * 3u));
    CHECK(output[center - 14u * 3u] == 20);
    CHECK(output[center] == 255 && output[center + 2] == 255);
    memset(input, 190, sizeof input);
    render(&p, W, H, input, output, 7);
    CHECK(!memcmp(input, output, sizeof input)); /* below threshold */
    memset(input, 20, sizeof input);
    input[center] = input[center + 1] = input[center + 2] = 255;
    p.streak = 0; p.bloom = 100; p.streak_radius = 64; p.vertical_radius = 1;
    render(&p, W, H, input, output, 2);
    CHECK(output[center + 6u * 3u + 2u] > 20);
    CHECK(output[center + W * 3u + 2u] > 20);
    CHECK(output[center + 2u * W * 3u + 2u] == 20);
    CHECK(output[center + 10u * 3u + 2u] == 20);
}

static void deterministic_test(void)
{
    uint8_t input[17 * 5 * 3], a[sizeof input], b[sizeof input];
    se_anamorphic p;
    size_t i;
    for (i = 0; i < sizeof input; i++) input[i] = (uint8_t)(i * 199u + 17u);
    se_anamorphic_preset(&p, 3, 1, 17);
    p.vertical_radius = 8; p.threshold = 0;
    p.chromatic = 100; p.vignette = 100; p.streak_radius = 2048;
    render(&p, 17, 5, input, a, 1);
    render(&p, 17, 5, input, b, 5);
    CHECK(!memcmp(a, b, sizeof a));
    { uint8_t one[3] = {255, 255, 255}, out[3];
      p.vignette = 0;
      render(&p, 1, 1, one, out, 1);
      CHECK(!memcmp(one, out, sizeof one));
      memset(one, 0, sizeof one);
      render(&p, 1, 1, one, out, 1);
      CHECK(!memcmp(one, out, sizeof one)); }
}

static void bounds_test(void)
{
    se_anamorphic p, q;
    se_anamorphic_run *run = NULL;
    se_arena ar;
    uint8_t input[3 * 3 * 3] = {0}, output[sizeof input], *memory;
    size_t large, small, cost;
    sink s = {output, 3, 3, 0, -1};
    se_anamorphic_preset(&p, 3, 0, 3);
    p.vertical_radius = 8;
    large = se_anamorphic_bytes(&p, 3001);
    q = p; q.vertical_radius = 0;
    small = se_anamorphic_bytes(&q, 3001);
    CHECK(large > small);
    CHECK(se_anamorphic_fit(&p, 3001, small) == small && !p.vertical_radius);
    CHECK(!se_anamorphic_fit(&p, 3001, small - 1u));
    CHECK(!se_anamorphic_bytes(&p, 0));
    CHECK(!se_anamorphic_bytes(&p, SE_ANAMORPHIC_MAX_WIDTH + 1));
    CHECK(!se_anamorphic_bytes(NULL, 1));
    p.strength = p.streak = p.bloom = p.vignette = p.chromatic = INT_MAX;
    p.threshold = INT_MIN; p.streak_radius = INT_MAX; p.vertical_radius = INT_MAX;
    cost = se_anamorphic_bytes(&p, 3);
    memory = (uint8_t *)malloc(cost);
    CHECK(memory);
    se_arena_init(&ar, memory, cost - 1u);
    CHECK(se_anamorphic_begin(&run, &p, 3, 3, &ar) == SE_E_NOMEM && !run);
    se_arena_init(&ar, memory, cost);
    CHECK(se_anamorphic_begin(&run, &p, 3, 0, &ar) == SE_E_CORRUPT);
    CHECK(se_anamorphic_begin(&run, &p, 3, SE_ANAMORPHIC_MAX_HEIGHT + 1, &ar) == SE_E_CORRUPT);
    CHECK(se_anamorphic_begin(&run, &p, 3, 3, &ar) == SE_OK);
    CHECK(se_anamorphic_end(run, collect, &s) == SE_E_TRUNCATED);
    CHECK(se_anamorphic_rows(run, input, 4, collect, &s) == SE_E_CORRUPT);
    CHECK(se_anamorphic_rows(run, NULL, 1, collect, &s) == SE_E_CORRUPT);
    CHECK(se_anamorphic_rows(run, input, 3, collect, &s) == SE_OK);
    CHECK(se_anamorphic_end(run, collect, &s) == SE_OK);
    free(memory);
    render(&p, 3, 3, input, output, 1);
    /* Largest accepted width, shortest height, extreme controls and radius. */
    { size_t n = SE_ANAMORPHIC_MAX_WIDTH * 3u;
      uint8_t *wide = (uint8_t *)malloc(n), *out = (uint8_t *)malloc(n);
      CHECK(wide && out); memset(wide, 255, n);
      render(&p, SE_ANAMORPHIC_MAX_WIDTH, 1, wide, out, 1);
      free(wide); free(out); }
}

static void error_test(void)
{
    uint8_t input[3 * 3 * 3] = {0}, output[sizeof input], *memory;
    se_anamorphic p;
    se_anamorphic_run *run = NULL;
    se_arena ar;
    sink s = {output, 3, 3, 0, 1};
    size_t cost;
    se_anamorphic_preset(&p, 0, 0, 3);
    cost = se_anamorphic_bytes(&p, 3);
    memory = (uint8_t *)malloc(cost);
    CHECK(memory);
    se_arena_init(&ar, memory, cost);
    CHECK(se_anamorphic_begin(&run, &p, 3, 3, &ar) == SE_OK);
    CHECK(se_anamorphic_rows(run, input, 3, collect, &s) == SE_E_IO);
    CHECK(se_anamorphic_rows(run, input, 1, collect, &s) == SE_E_IO);
    CHECK(se_anamorphic_end(run, collect, &s) == SE_E_IO);
    free(memory);
}

int main(void)
{
    neutral_test(); highlight_test(); deterministic_test();
    bounds_test(); error_test();
    puts("anamorphic: neutral, highlights, bloom, determinism, bounds, errors passed");
    return 0;
}
