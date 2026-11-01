/* Desktop reference for the T6 still-photo pipeline. GPL-2.0.
 * Uses the same streaming film/anamorphic C sources as the camera build.
 * The optional optical desqueeze and output crop are desktop operations.
 */
#define _POSIX_C_SOURCE 200809L
#include "se.h"
#include "cr2.h"
#include "stock.h"
#include "look.h"
#include "anamorphic.h"
#include "t6_profile.h"
#include "jpeg_dec.h"
#include "jpeg_enc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>

#define FILE_CAP (128u * 1024u * 1024u)
#define ARENA_CAP (8u * 1024u * 1024u)
#define FX_BUDGET SE_T6_EFFECT_BUDGET
#define DIM_CAP 16384

typedef struct {
    se_jpeg *enc;
    se_look_run *look;
    se_anamorphic_run *anam;
    uint8_t *resampled;
    int width, out_width, height, top, out_height, row, emitted;
} pipeline;

static void usage(FILE *f)
{
    fprintf(f, "T6 Cinema desktop reference (stills, hardware-untested camera port)\n"
        "Usage: t6dev input.CR2|JPG -o output.JPG [options]\n"
        "  --stock file.fs       Soup film recipe; omitted = neutral color\n"
        "  --anamorphic off|subtle|cinema|strong  (default cinema)\n"
        "  --tint blue|amber     flare tint (default blue)\n"
        "  --scale 1|2|4         JPEG decode scale (default 2)\n"
        "  --desqueeze 1|1.33|1.5|1.6|1.8|2  actual squeezed optics ONLY\n"
        "  --aspect native|2.39|2.0|16:9       center crop (default native)\n"
        "  --quality 1..100      JPEG quality (default 88, camera match)\n"
        "Existing outputs and input files are never overwritten.\n"
        "Effects emulate streaks/bloom/fringing/vignette; no depth or optical bokeh.\n");
}

static void *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long size;
    void *p;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) <= 0 ||
        (unsigned long)size > FILE_CAP || fseek(f, 0, SEEK_SET)) {
        fclose(f); return NULL;
    }
    p = malloc((size_t)size);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)size, f) != (size_t)size) {
        fclose(f); free(p); return NULL;
    }
    fclose(f); *len = (size_t)size; return p;
}

static int write_bytes(void *ctx, const uint8_t *data, size_t size)
{
    return fwrite(data, 1, size, (FILE *)ctx) == size ? 0 : -1;
}

static int output_row(void *ctx, const uint8_t *rgb, int width)
{
    pipeline *p = ctx;
    int y = p->row++, x, c;
    if (width != p->width || y >= p->height) return -1;
    if (y < p->top || y >= p->top + p->out_height) return 0;
    if (p->out_width != width) {
        /* Linear horizontal resampling is for real squeezed source images.
         * It is deliberately not part of the emulation preset. */
        for (x = 0; x < p->out_width; x++) {
            uint64_t q = (uint64_t)x * (unsigned)(width - 1) * 65536u /
                         (unsigned)(p->out_width - 1);
            int a = (int)(q >> 16), b = a + 1 < width ? a + 1 : a;
            unsigned frac = (unsigned)(q & 65535u);
            for (c = 0; c < 3; c++)
                p->resampled[x * 3 + c] = (uint8_t)(
                    ((uint32_t)rgb[a * 3 + c] * (65536u - frac) +
                     (uint32_t)rgb[b * 3 + c] * frac + 32768u) >> 16);
        }
        rgb = p->resampled;
    }
    if (se_jpeg_write_rows(p->enc, rgb, 1) != SE_OK) return -1;
    p->emitted++;
    return 0;
}

static int film_row(void *ctx, const uint8_t *rgb, int width)
{
    pipeline *p = ctx;
    if (width != p->width) return -1;
    return se_anamorphic_rows(p->anam, rgb, 1, output_row, p) == SE_OK ? 0 : -1;
}

static int decoded_rows(void *ctx, const uint8_t *rgb, int rows, int width)
{
    pipeline *p = ctx;
    if (width != p->width) return -1;
    return se_look_rows(p->look, rgb, rows, film_row, p) == SE_OK ? 0 : -1;
}

static int choice(const char *s, const char *const *names, int n)
{
    int i;
    for (i = 0; i < n; i++) if (!strcmp(s, names[i])) return i;
    return -1;
}

static int integer(const char *s, int lo, int hi)
{
    char *end;
    long n;
    errno = 0; n = strtol(s, &end, 10);
    return errno || !*s || *end || n < lo || n > hi ? -1 : (int)n;
}

int main(int argc, char **argv)
{
    const char *in = NULL, *out = NULL, *stock_path = NULL;
    const char *const modes[] = {"off", "subtle", "cinema", "strong"};
    const char *const tints[] = {"blue", "amber"};
    const char *const squeezes[] = {"1", "1.33", "1.5", "1.6", "1.8", "2"};
    const int squeeze_values[] = {100, 133, 150, 160, 180, 200};
    const char *const aspects[] = {"native", "2.39", "2.0", "16:9"};
    int preset = 2, tint = 0, scale = SE_T6_DECODE_SCALE, squeeze = 100, aspect = 0, quality = 88;
    int i, rc = 1, w, h, crop_h, outw, fd = -1, created = 0;
    uint8_t *file = NULL, *jpeg, *memory = NULL, *strip;
    void *stock_text = NULL;
    size_t size = 0, jpeg_size, stock_size = 0;
    uint32_t off = 0, bytes = 0;
    se_jdec_info ji;
    se_cr2_info info;
    se_stock stock;
    se_look look;
    se_anamorphic anam;
    se_arena arena;
    se_jpeg_exif ex;
    pipeline p;
    FILE *fo = NULL;
    clock_t started = clock();
    se_err err = SE_OK;
    memset(&p, 0, sizeof p); memset(&info, 0, sizeof info);
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "--help"))) {
        usage(stdout); return argc == 1 ? 2 : 0;
    }
    for (i = 1; i < argc; i++) {
        const char *key = argv[i], *val;
        int n;
        if (key[0] != '-') {
            if (in) { fprintf(stderr, "Only one input is accepted.\n"); return 2; }
            in = key; continue;
        }
        if (i + 1 >= argc) { usage(stderr); return 2; }
        val = argv[++i];
        if (!strcmp(key, "-o")) out = val;
        else if (!strcmp(key, "--stock")) stock_path = val;
        else if (!strcmp(key, "--anamorphic")) {
            preset = choice(val, modes, 4); if (preset < 0) goto bad_option;
        } else if (!strcmp(key, "--tint")) {
            tint = choice(val, tints, 2); if (tint < 0) goto bad_option;
        } else if (!strcmp(key, "--scale")) {
            scale = integer(val, 1, 4);
            if (scale != 1 && scale != 2 && scale != 4) goto bad_option;
        } else if (!strcmp(key, "--quality")) {
            quality = integer(val, 1, 100); if (quality < 0) goto bad_option;
        } else if (!strcmp(key, "--desqueeze")) {
            n = choice(val, squeezes, 6); if (n < 0) goto bad_option;
            squeeze = squeeze_values[n];
        } else if (!strcmp(key, "--aspect")) {
            aspect = choice(val, aspects, 4); if (aspect < 0) goto bad_option;
        } else { goto bad_option; }
        continue;
bad_option:
        fprintf(stderr, "Invalid option: %s %s\n", key, val); return 2;
    }
    if (!in || !out) { usage(stderr); return 2; }
    file = read_file(in, &size);
    if (!file) { fprintf(stderr, "Cannot read input (limit 128 MiB): %s\n", in); goto done; }
    jpeg = file; jpeg_size = size;
    if (size < 2 || file[0] != 0xff || file[1] != 0xd8) {
        if (se_cr2_find_preview(file, size, &off, &bytes, &w, &h) != SE_OK ||
            off > size || bytes > size - off) {
            fprintf(stderr, "No supported CR2 embedded JPEG.\n"); goto done;
        }
        jpeg = file + off; jpeg_size = bytes;
        if (se_cr2_parse(file, size, &info) != SE_OK) memset(&info, 0, sizeof info);
    }
    if ((err = se_jpeg_dec_info(jpeg, jpeg_size, &ji)) != SE_OK) goto engine_error;
    if (ji.width > DIM_CAP || ji.height > DIM_CAP || ji.width < 16 || ji.height < 8) {
        fprintf(stderr, "Unsupported image dimensions.\n"); goto done;
    }
    se_jpeg_scaled_size(ji.width, ji.height, scale, &w, &h);
    outw = (int)((int64_t)w * squeeze / 100);
    if (outw > DIM_CAP || outw < 16 || h < 8) {
        fprintf(stderr, "Output dimensions out of range.\n"); goto done;
    }
    crop_h = h;
    if (aspect == 1) crop_h = outw * 100 / 239;
    else if (aspect == 2) crop_h = outw / 2;
    else if (aspect == 3) crop_h = outw * 9 / 16;
    if (crop_h > h) crop_h = h;
    crop_h &= ~7;
    if (crop_h < 8) { fprintf(stderr, "Crop is too small.\n"); goto done; }
    se_stock_init(&stock);
    if (stock_path) {
        stock_text = read_file(stock_path, &stock_size);
        if (!stock_text || stock_size > 65536u ||
            se_stock_parse(stock_text, stock_size, &stock) != SE_OK) {
            fprintf(stderr, "Invalid film recipe.\n"); goto done;
        }
    }
    memory = malloc(ARENA_CAP);
    if (!memory) { fprintf(stderr, "Out of memory.\n"); goto done; }
    se_arena_init(&arena, memory, ARENA_CAP);
    se_look_from_stock(&stock, scale, &look);
    se_look_fit(&look, w, SE_T6_LOOK_BUDGET);
    se_anamorphic_preset(&anam, preset, tint, w);
    if (!se_anamorphic_fit(&anam, w, FX_BUDGET)) { err = SE_E_NOMEM; goto engine_error; }
    p.width = w; p.height = h; p.out_width = outw;
    p.out_height = crop_h; p.top = (h - crop_h) / 2;
    p.enc = SE_NEW(&arena, se_jpeg, 1);
    strip = SE_NEW(&arena, uint8_t, (size_t)8 * outw * 3);
    p.resampled = SE_NEW(&arena, uint8_t, (size_t)outw * 3);
    if (!p.enc || !strip || !p.resampled) { err = SE_E_NOMEM; goto engine_error; }
    if ((err = se_look_begin(&p.look, &look, w, &arena)) != SE_OK ||
        (err = se_anamorphic_begin(&p.anam, &anam, w, h, &arena)) != SE_OK)
        goto engine_error;
    /* O_EXCL rejects existing files, symlinks and hardlinks to the input. */
    fd = open(out, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) { fprintf(stderr, "Output must be a NEW file: %s (%s)\n", out, strerror(errno)); goto done; }
    created = 1; fo = fdopen(fd, "wb");
    if (!fo) { close(fd); fd = -1; goto done; }
    fd = -1;
    memset(&ex, 0, sizeof ex);
    ex.make = info.make[0] ? info.make : NULL;
    ex.model = info.model[0] ? info.model : NULL;
    ex.software = "T6 Cinema 0.1.0-dev"; ex.datetime = info.datetime;
    ex.iso = info.iso; ex.aperture = info.aperture; ex.exposure_time = info.exposure_time;
    if ((err = se_jpeg_begin(p.enc, outw, crop_h, quality, strip, &ex, write_bytes, fo)) != SE_OK ||
        (err = se_jpeg_decode_scaled(jpeg, jpeg_size, scale, &arena, decoded_rows, &p, &ji)) != SE_OK ||
        (err = se_look_end(p.look, film_row, &p)) != SE_OK ||
        (err = se_anamorphic_end(p.anam, output_row, &p)) != SE_OK ||
        (err = se_jpeg_end(p.enc)) != SE_OK) goto engine_error;
    if (p.emitted != crop_h || p.row != h) { err = SE_E_CORRUPT; goto engine_error; }
    if (fclose(fo)) { fo = NULL; fprintf(stderr, "Output write failed.\n"); goto done; }
    fo = NULL;
    printf("%s -> %s\n", in, out);
    printf("source=%s decoded=%dx%d output=%dx%d preset=%s tint=%s stock=%s\n",
        info.model[0] ? info.model : "JPEG", w, h, p.enc->width, p.enc->height,
        modes[preset], tints[tint], stock_path ? stock.name : "neutral");
    printf("streaming_arena_peak=%zu bytes cpu_seconds=%.3f\n", arena.peak,
        (double)(clock() - started) / CLOCKS_PER_SEC);
    rc = 0; goto done;
engine_error:
    fprintf(stderr, "Image engine failed: %s (%d)\n", se_strerror(err), err);
done:
    if (fo) fclose(fo);
    if (fd >= 0) close(fd);
    if (rc && created) unlink(out);
    free(stock_text); free(memory); free(file);
    return rc;
}
