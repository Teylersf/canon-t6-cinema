/* Custom synthetic-only host Kvazaar experiment. GPL-2.0-only.
 * Static-link allocation instrumentation is not a camera allocator adapter.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "kvazaar.h"

#define MAX_LIVE_ALLOCATIONS 16384u
typedef struct { void *pointer; size_t bytes; } allocation;
static allocation live[MAX_LIVE_ALLOCATIONS];
static size_t allocated, peak, budget, requests, created_threads;
void *__real_malloc(size_t bytes);
void *__real_realloc(void *pointer, size_t bytes);
void __real_free(void *pointer);

static void allocation_stop(const char *why, int code)
{
    char message[256];
    int n = snprintf(message, sizeof(message),
        "{\"status\":\"%s\",\"requested_live_bytes\":%zu,\"requested_peak_bytes\":%zu,"
        "\"budget_bytes\":%zu,\"allocation_requests\":%zu}\n",
        why, allocated, peak, budget, requests);
    if (n > 0 && (size_t)n < sizeof(message)) {
        ssize_t sent = write(STDERR_FILENO, message, (size_t)n);
        (void)sent;
    }
    _Exit(code); /* Host-process boundary: upstream allocation failure is not a safe unwind API. */
}

static size_t find_allocation(void *pointer)
{
    size_t i;
    for (i = 0; i < MAX_LIVE_ALLOCATIONS; ++i)
        if (live[i].pointer == pointer) return i;
    allocation_stop("unknown_allocation", 71);
    return 0;
}

void *__wrap_malloc(size_t bytes)
{
    size_t i;
    void *pointer;
    ++requests;
    if (bytes > budget - allocated) allocation_stop("allocation_budget", 70);
    for (i = 0; i < MAX_LIVE_ALLOCATIONS && live[i].pointer; ++i) {}
    if (i == MAX_LIVE_ALLOCATIONS) allocation_stop("allocation_registry_limit", 70);
    pointer = __real_malloc(bytes ? bytes : 1u);
    if (!pointer) allocation_stop("host_allocation_failed", 70);
    live[i].pointer = pointer;
    live[i].bytes = bytes;
    allocated += bytes;
    if (allocated > peak) peak = allocated;
    return pointer;
}

void *__wrap_calloc(size_t count, size_t bytes)
{
    void *pointer;
    if (count && bytes > SIZE_MAX / count) allocation_stop("allocation_overflow", 70);
    bytes *= count;
    pointer = __wrap_malloc(bytes);
    memset(pointer, 0, bytes);
    return pointer;
}

void __wrap_free(void *pointer)
{
    size_t i;
    if (!pointer) return;
    i = find_allocation(pointer);
    allocated -= live[i].bytes;
    live[i].pointer = NULL;
    live[i].bytes = 0;
    __real_free(pointer);
}

void *__wrap_realloc(void *pointer, size_t bytes)
{
    size_t i, old;
    void *result;
    if (!pointer) return __wrap_malloc(bytes);
    if (!bytes) { __wrap_free(pointer); return NULL; }
    i = find_allocation(pointer);
    old = live[i].bytes;
    ++requests;
    if (bytes > budget - (allocated - old)) allocation_stop("allocation_budget", 70);
    result = __real_realloc(pointer, bytes);
    if (!result) allocation_stop("host_allocation_failed", 70);
    live[i].pointer = result;
    live[i].bytes = bytes;
    allocated = allocated - old + bytes;
    if (allocated > peak) peak = allocated;
    return result;
}

char *__wrap_strdup(const char *text)
{
    size_t bytes = strlen(text) + 1u;
    char *copy = __wrap_malloc(bytes);
    memcpy(copy, text, bytes);
    return copy;
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*entry)(void *), void *argument)
{
    (void)thread; (void)attr; (void)entry; (void)argument;
    ++created_threads;
    allocation_stop("unexpected_worker_thread", 72);
    return EAGAIN;
}

static uint64_t monotonic_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) allocation_stop("host_clock_failed", 73);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static int number(const char *text, size_t minimum, size_t maximum, size_t *out)
{
    char *end;
    unsigned long long value;
    if (!*text || *text == '-' || *text == '+') return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno || *end || value < minimum || value > maximum) return 0;
    *out = (size_t)value;
    return 1;
}

static void fill(kvz_picture *picture, unsigned frame)
{
    unsigned x, y;
    for (y = 0; y < (unsigned)picture->height; ++y) {
        for (x = 0; x < (unsigned)picture->width; ++x) {
            unsigned value = 16u + ((x / 8u + y / 8u + frame * 9u) % 180u);
            if ((x + frame * 8u) % 128u < 8u) value = 235u;
            picture->y[(size_t)y * (size_t)picture->stride + x] = (kvz_pixel)value;
        }
    }
    for (y = 0; y < (unsigned)picture->height / 2u; ++y) {
        for (x = 0; x < (unsigned)picture->width / 2u; ++x) {
            size_t at = (size_t)y * (size_t)picture->stride / 2u + x;
            picture->u[at] = (kvz_pixel)(96u + (frame % 3u) * 8u);
            picture->v[at] = 144u;
        }
    }
}

static int write_chunks(FILE *output, kvz_data_chunk *chunks, uint32_t length,
                        size_t limit, size_t *written)
{
    kvz_data_chunk *chunk;
    size_t sum = 0, count = 0;
    for (chunk = chunks; chunk; chunk = chunk->next) {
        if (++count > 16384u || chunk->len > (size_t)length - sum) return 5;
        sum += chunk->len;
    }
    if (sum != length) return 5;
    if (sum > limit - *written) return 4;
    for (chunk = chunks; chunk; chunk = chunk->next) {
        if (fwrite(chunk->data, 1, chunk->len, output) != chunk->len) return 5;
        *written += chunk->len;
    }
    return 0;
}

int main(int argc, char **argv)
{
    size_t width, height, frames, limit, cancel_after;
    size_t written = 0, submitted = 0, encoded = 0, iteration;
    uint64_t start, encode_ns = 0;
    const kvz_api *api = NULL;
    kvz_config *config = NULL;
    kvz_encoder *encoder = NULL;
    FILE *output = NULL;
    int result = 5;
    if (argc != 8 || !number(argv[2], 64u, 8192u, &width) ||
        !number(argv[3], 64u, 8192u, &height) || width % 8u || height % 8u ||
        !number(argv[4], 1u, 3u, &frames) ||
        !number(argv[5], 1u, 1024u * 1024u * 1024u, &budget) ||
        !number(argv[6], 1u, 64u * 1024u * 1024u, &limit) ||
        !number(argv[7], 0u, 4u, &cancel_after)) {
        fprintf(stderr, "usage: harness NEW.hevc width height frames allocation_budget output_limit cancel_after(4=never)\n");
        return 2;
    }
    output = fopen(argv[1], "wx"); /* Never replace an existing host file. */
    if (!output) return 5;
    start = monotonic_ns();
    if (!cancel_after) { result = 3; goto done; }
    api = kvz_api_get(8);
    if (!api || !(config = api->config_alloc()) || !api->config_init(config) ||
        !api->config_parse(config, "preset", "ultrafast") ||
        !api->config_parse(config, "level", "6.2")) goto done;
    config->width = (int32_t)width;
    config->height = (int32_t)height;
    config->threads = 0;
    config->wpp = 0;
    config->owf = 0;
    config->gop_len = 0;
    config->intra_period = 1;
    config->vps_period = 0;
    config->ref_frames = 1;
    config->cpuid = 0; /* Generic C, no x86 SIMD selection for these measurements. */
    config->add_encoder_info = 0;
    config->calc_psnr = 0;
    config->framerate_num = 30;
    config->framerate_denom = 1;
    if (!(encoder = api->encoder_open(config))) goto done;
    for (iteration = 0; iteration < frames + 4u; ++iteration) {
        kvz_picture *picture = NULL;
        kvz_data_chunk *chunks = NULL;
        uint32_t length = 0;
        uint64_t before;
        int ok;
        result = 5;
        if (submitted >= cancel_after) { result = 3; goto done; }
        if (submitted < frames) {
            picture = api->picture_alloc((int32_t)width, (int32_t)height);
            if (!picture) goto done;
            fill(picture, (unsigned)submitted);
        }
        before = monotonic_ns();
        ok = api->encoder_encode(encoder, picture, &chunks, &length, NULL, NULL, NULL);
        encode_ns += monotonic_ns() - before;
        if (picture) { api->picture_free(picture); ++submitted; }
        if (!ok) { api->chunk_free(chunks); goto done; }
        if (length) ++encoded;
        result = write_chunks(output, chunks, length, limit, &written);
        api->chunk_free(chunks);
        if (result) goto done;
        if (!picture && !length) break;
    }
    if (submitted != frames || encoded != frames) result = 5;
done:
    if (encoder) api->encoder_close(encoder);
    if (config) api->config_destroy(config);
    if (fclose(output) && !result) result = 5;
    if (allocated && !result) result = 6;
    printf("{\"status_code\":%d,\"width\":%zu,\"height\":%zu,\"frames_submitted\":%zu,"
           "\"frames_encoded\":%zu,\"output_bytes\":%zu,\"output_limit_bytes\":%zu,"
           "\"allocation_budget_bytes\":%zu,\"requested_peak_bytes\":%zu,\"requested_live_bytes\":%zu,"
           "\"allocation_requests\":%zu,\"created_worker_threads\":%zu,"
           "\"host_encode_ns\":%llu,\"host_total_ns\":%llu,\"camera_fps_measured\":false}\n",
           result, width, height, submitted, encoded, written, limit, budget, peak, allocated,
           requests, created_threads, (unsigned long long)encode_ns,
           (unsigned long long)(monotonic_ns() - start));
    return result;
}
