/* Full-size still JPEG to bounded 4K HEVC input. GPL-2.0-only.
 * Host POSIX file adapter only: no camera bridge or live capture interface. */
#define _POSIX_C_SOURCE 200809L
#include "se.h"
#include "cr2.h"
#include "jpeg_dec.h"
#include "anamorphic.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FS_WIDTH 3840u
#define FS_HEIGHT 2160u
#define FS_HEADER (128u * 1024u)
#define FS_DECODE (768u * 1024u)
#define FS_EFFECT (384u * 1024u)
#define FS_INPUT_LIMIT (64u * 1024u * 1024u)
#define FS_JPEG_LIMIT (12u * 1024u * 1024u)
#define FS_SOURCE_WIDTH 6000u
#define FS_SOURCE_HEIGHT 4000u
#define FS_Y_BYTES ((uint64_t)FS_WIDTH * FS_HEIGHT)
#define FS_C_BYTES (FS_Y_BYTES / 4u)
#define FS_FRAME_BYTES (FS_Y_BYTES + 2u * FS_C_BYTES)

typedef struct {
    int input, output;
    uint32_t jpeg_offset, jpeg_length;
    unsigned source_width, source_height, left, top;
    unsigned input_rows, crop_rows, output_rows;
    size_t output_prefix;
    uint8_t previous[FS_WIDTH * 3u];
    uint8_t y[FS_WIDTH], cb[FS_WIDTH / 2u], cr[FS_WIDTH / 2u];
    se_anamorphic_run *effect;
} frame_source;

static int read_exact_at(int file, void *data, size_t bytes, uint64_t offset)
{
    size_t done = 0;
    while (done < bytes) {
        ssize_t n = pread(file, (uint8_t *)data + done, bytes - done, (off_t)(offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || (size_t)n > bytes - done) return -1;
        done += (size_t)n;
    }
    return 0;
}

static int write_exact_at(int file, const void *data, size_t bytes, uint64_t offset)
{
    size_t done = 0;
    if (offset > FS_FRAME_BYTES + 256u || bytes > FS_FRAME_BYTES + 256u - offset) return -1;
    while (done < bytes) {
        ssize_t n = pwrite(file, (const uint8_t *)data + done, bytes - done, (off_t)(offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || (size_t)n > bytes - done) return -1;
        done += (size_t)n;
    }
    return 0;
}

static int jpeg_read(void *context, uint32_t offset, void *data, uint32_t bytes)
{
    frame_source *source = context;
    if (offset > source->jpeg_length || bytes > source->jpeg_length - offset || bytes > 8192u) return -1;
    return read_exact_at(source->input, data, bytes, (uint64_t)source->jpeg_offset + offset);
}

/* Explicit floor division avoids implementation-defined signed right shifts.
 * Inputs below are bounded to [-114240,114240], far inside int32_t. */
static int floor_div(int value, int denominator)
{
    return value >= 0 ? value / denominator : -((-value + denominator - 1) / denominator);
}

static uint8_t clip(int value, int low, int high)
{
    return (uint8_t)(value < low ? low : value > high ? high : value);
}

/* BT.601 studio-range integer approximation. RGB is nonlinear RGB8, not
 * linear light. Chroma averages each centered 2x2 group before quantization. */
static int emit_rgb(void *context, const uint8_t *rgb, int width)
{
    frame_source *source = context;
    unsigned x, row = source->output_rows;
    uint64_t base = source->output_prefix;
    if (!rgb || width != (int)FS_WIDTH || row >= FS_HEIGHT) return -1;
    for (x = 0; x < FS_WIDTH; ++x) {
        const uint8_t *p = rgb + (size_t)x * 3u;
        source->y[x] = clip(16 + floor_div(66 * p[0] + 129 * p[1] + 25 * p[2] + 128, 256), 16, 235);
    }
    if (write_exact_at(source->output, source->y, FS_WIDTH, base + (uint64_t)row * FS_WIDTH)) return -1;
    if (!(row & 1u)) memcpy(source->previous, rgb, sizeof(source->previous));
    else {
        for (x = 0; x < FS_WIDTH; x += 2u) {
            const uint8_t *a = source->previous + (size_t)x * 3u;
            const uint8_t *b = rgb + (size_t)x * 3u;
            int r = a[0] + a[3] + b[0] + b[3];
            int g = a[1] + a[4] + b[1] + b[4];
            int blue = a[2] + a[5] + b[2] + b[5];
            source->cb[x / 2u] = clip(128 + floor_div(-38 * r - 74 * g + 112 * blue + 512, 1024), 16, 240);
            source->cr[x / 2u] = clip(128 + floor_div(112 * r - 94 * g - 18 * blue + 512, 1024), 16, 240);
        }
        if (write_exact_at(source->output, source->cb, FS_WIDTH / 2u,
                           base + FS_Y_BYTES + (uint64_t)(row / 2u) * (FS_WIDTH / 2u)) ||
            write_exact_at(source->output, source->cr, FS_WIDTH / 2u,
                           base + FS_Y_BYTES + FS_C_BYTES + (uint64_t)(row / 2u) * (FS_WIDTH / 2u))) return -1;
    }
    ++source->output_rows;
    return 0;
}

static int decoded_rgb(void *context, const uint8_t *rgb, int rows, int width)
{
    frame_source *source = context;
    int i;
    if (!rgb || width != (int)source->source_width || rows < 1 || rows > 32 ||
        (unsigned)rows > source->source_height - source->input_rows) return -1;
    for (i = 0; i < rows; ++i) {
        unsigned y = source->input_rows++;
        if (y >= source->top && y - source->top < FS_HEIGHT) {
            const uint8_t *row = rgb + ((size_t)i * source->source_width + source->left) * 3u;
            if (se_anamorphic_rows(source->effect, row, 1, emit_rgb, source) != SE_OK) return -1;
            ++source->crop_rows;
        }
    }
    return 0;
}

static void usage(void)
{
    fputs("Usage: hevc-frame-source --input source.CR2|JPEG --output NEW.y4m|yuv\n"
          "       [--format y4m|raw] [--effect none|strong]\n"
          "Strict 3840x2160 center crop; no upscaling or live capture.\n", stderr);
}

int main(int argc, char **argv)
{
    const char *input = NULL, *output = NULL;
    int format_y4m = 1, strong = 0, i, rc = 1, created = 0;
    struct stat before, after, final;
    frame_source *source = NULL;
    uint8_t *header = NULL, *decode_memory = NULL, *effect_memory = NULL;
    se_arena decode_arena, effect_arena;
    se_anamorphic effect;
    se_jdec_info jpeg;
    size_t got, jpeg_header, effect_cost;
    uint32_t offset = 0, length = 0;
    static const char y4m_header[] = "YUV4MPEG2 W3840 H2160 F1:1 Ip A1:1 C420jpeg XCOLORRANGE=LIMITED XCOLORMATRIX=BT601\nFRAME\n";
    for (i = 1; i < argc; ++i) {
        const char *option = argv[i], *value;
        if (!strcmp(option, "--help")) { usage(); return 0; }
        if (i + 1 >= argc) { usage(); return 2; }
        value = argv[++i];
        if (!strcmp(option, "--input") && !input) input = value;
        else if (!strcmp(option, "--output") && !output) output = value;
        else if (!strcmp(option, "--format") && (!strcmp(value, "raw") || !strcmp(value, "y4m"))) format_y4m = !strcmp(value, "y4m");
        else if (!strcmp(option, "--effect") && (!strcmp(value, "none") || !strcmp(value, "strong"))) strong = !strcmp(value, "strong");
        else { usage(); return 2; }
    }
    if (!input || !output) { usage(); return 2; }
    source = calloc(1, sizeof(*source));
    if (!source) goto done;
    source->input = source->output = -1;
    /* Nonblocking open prevents a FIFO from stalling before the regular-file
     * check. POSIX regular-file reads retain their usual behavior. */
    source->input = open(input, O_RDONLY | O_NONBLOCK);
    if (source->input < 0 || fstat(source->input, &before) || !S_ISREG(before.st_mode) ||
        before.st_size < 16 || (uint64_t)before.st_size > FS_INPUT_LIMIT) {
        fputs("Input must be a regular file of 16 bytes to 64 MiB.\n", stderr); goto done;
    }
    header = malloc(FS_HEADER); decode_memory = malloc(FS_DECODE); effect_memory = malloc(FS_EFFECT);
    if (!header || !decode_memory || !effect_memory) { fputs("Bounded arena allocation failed.\n", stderr); goto done; }
    got = (uint64_t)before.st_size < FS_HEADER ? (size_t)before.st_size : FS_HEADER;
    if (read_exact_at(source->input, header, got, 0)) { fputs("Input header read failed.\n", stderr); goto done; }
    if (header[0] == 0xff && header[1] == 0xd8) length = (uint32_t)before.st_size;
    else if (se_cr2_find_preview(header, got, &offset, &length, NULL, NULL) != SE_OK) {
        fputs("No supported full-size CR2 JPEG.\n", stderr); goto done;
    }
    if (offset >= got || !length || length > FS_JPEG_LIMIT ||
        offset > (uint64_t)before.st_size || length > (uint64_t)before.st_size - offset) {
        fputs("Embedded JPEG exceeds input bounds.\n", stderr); goto done;
    }
    jpeg_header = got - offset;
    if (jpeg_header > length) jpeg_header = length;
    if (se_jpeg_dec_info(header + offset, jpeg_header, &jpeg) != SE_OK ||
        jpeg.width < (int)FS_WIDTH || jpeg.height < (int)FS_HEIGHT ||
        jpeg.width > (int)FS_SOURCE_WIDTH || jpeg.height > (int)FS_SOURCE_HEIGHT) {
        fputs("Need a supported JPEG at least 3840x2160 and at most 6000x4000; upscaling refused.\n", stderr); goto done;
    }
    source->jpeg_offset = offset; source->jpeg_length = length;
    source->source_width = (unsigned)jpeg.width; source->source_height = (unsigned)jpeg.height;
    /* Even crop origins keep 2x2 chroma groups aligned to source coordinates. */
    source->left = ((source->source_width - FS_WIDTH) / 2u) & ~1u;
    source->top = ((source->source_height - FS_HEIGHT) / 2u) & ~1u;
    source->output_prefix = format_y4m ? sizeof(y4m_header) - 1u : 0u;
    se_arena_init(&decode_arena, decode_memory, FS_DECODE);
    se_arena_init(&effect_arena, effect_memory, FS_EFFECT);
    se_anamorphic_preset(&effect, strong ? 3 : 0, 0, (int)FS_WIDTH);
    effect_cost = se_anamorphic_fit(&effect, (int)FS_WIDTH, FS_EFFECT);
    if (!effect_cost || se_anamorphic_begin(&source->effect, &effect, (int)FS_WIDTH,
                                           (int)FS_HEIGHT, &effect_arena) != SE_OK) {
        fputs("Effect does not fit its fixed arena.\n", stderr); goto done;
    }
    source->output = open(output, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (source->output < 0) { fputs("Output must be a NEW file; existing paths are preserved.\n", stderr); goto done; }
    created = 1;
    if (format_y4m && write_exact_at(source->output, y4m_header, source->output_prefix, 0)) goto io_error;
    if (se_jpeg_decode_stream_scaled(header + offset, jpeg_header, length, jpeg_read, source,
                                     1, &decode_arena, decoded_rgb, source, &jpeg) != SE_OK ||
        source->input_rows != source->source_height || source->crop_rows != FS_HEIGHT ||
        se_anamorphic_end(source->effect, emit_rgb, source) != SE_OK || source->output_rows != FS_HEIGHT) {
        fputs("Streaming decode/effect failed or did not deliver the whole crop.\n", stderr); goto done;
    }
    if (fstat(source->input, &after) || before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
        before.st_size != after.st_size || fstat(source->output, &final) ||
        (uint64_t)final.st_size != source->output_prefix + FS_FRAME_BYTES || fsync(source->output)) goto io_error;
    if (close(source->output)) { source->output = -1; goto io_error; }
    source->output = -1;
    printf("{\"source_width\":%u,\"source_height\":%u,\"crop_left\":%u,\"crop_top\":%u,"
           "\"width\":3840,\"height\":2160,\"frames\":1,\"effect\":\"%s\",\"format\":\"%s\","
           "\"matrix\":\"BT601\",\"range\":\"limited\",\"chroma\":\"centered_2x2_box\","
           "\"plane_bytes\":%" PRIu64 ",\"file_bytes\":%" PRIu64 ",\"decode_arena_peak\":%zu,"
           "\"effect_arena_peak\":%zu,\"owned_heap_budget\":%zu,\"still_derived\":true,\"live_capture\":false}\n",
           source->source_width, source->source_height, source->left, source->top,
           strong ? "strong_blue" : "none", format_y4m ? "y4m" : "raw_yuv420p",
           FS_FRAME_BYTES, source->output_prefix + FS_FRAME_BYTES, decode_arena.peak,
           effect_arena.peak, sizeof(*source) + (size_t)FS_HEADER + FS_DECODE + FS_EFFECT);
    rc = 0; goto done;
io_error:
    fputs("Input/output verification failed.\n", stderr);
done:
    if (source) {
        if (source->output >= 0) close(source->output);
        if (source->input >= 0) close(source->input);
    }
    if (rc && created) fputs("The newly created output may be partial; it was retained and must not be treated as complete.\n", stderr);
    free(effect_memory); free(decode_memory); free(header); free(source);
    return rc;
}
