/** T6 Cinema: bounded JPEG-preview processing using the pinned T6 ML APIs.
 * GPL-2.0. CR2 sources are opened read-only. Only our own new TMP is removed.
 * This is a still-image worker; it never changes sensor or movie registers.
 */
#include "dryos.h"
#include "mem.h"
#include "fio-ml.h"
#include "fileprefix.h"
#include "lens.h"
#include "propvalues.h"
#include "notify_box.h"
#include "film_t6.h"
#include "film_cpu_bench.h"
#include "se.h"
#include "cr2.h"
#include "stock.h"
#include "look.h"
#include "anamorphic.h"
#include "t6_profile.h"
#include "jpeg_dec.h"
#include "jpeg_enc.h"

#if !defined(CONFIG_1300D_110)
#error T6 Cinema must only be compiled for 1300D firmware 1.1.0
#endif

#define FILM_HEADER (128 * 1024)
#define FILM_DECODE SE_T6_DECODE_BUDGET
#define FILM_LOOK SE_T6_LOOK_BUDGET
#define FILM_EFFECT SE_T6_EFFECT_BUDGET
#define FILM_IO 8192
#define FILM_SCALE SE_T6_SCALE
#define FILM_MAX_INPUT (64u * 1024 * 1024)
#define FILM_MAX_PREVIEW (12u * 1024 * 1024)
#define FILM_MAX_OUTPUT (8u * 1024 * 1024)
#define FILM_MAX_BATCH 4
#define FILM_DEADLINE_MS (5u * 60 * 1000)
/* 1300D.110 native flags, verified with its actual ROM in the emulator:
 * write=0x0001, create=0x0100, exclusive=0x0400, check-existing=0x8000.
 * Truncate 0x0200 is deliberately ABSENT. The global ML O_CREAT/O_EXCL
 * definitions differ; do not change those camera-independent macros. */
#define FILM_T6_CREATE_NEW 0x8501u

extern int ml_started;

typedef struct {
    FILE *source;
    FILE *output;
    uint8_t *io;
    uint32_t preview_offset, preview_length, written, started;
    int automatic;
    int width, height, rows;
    se_jpeg *encoder;
    se_look_run *look;
    se_anamorphic_run *effect;
} film_job;

static int film_camera_busy(void)
{
    return lens_info.job_state != 0 || get_halfshutter_pressed() || is_movie_mode();
}

static int film_abort(film_job *job)
{
    return film_t6_cancelled() || film_camera_busy() ||
        (job->automatic && !film_t6_auto_enabled()) ||
        ((uint32_t)get_ms_clock() - job->started >= FILM_DEADLINE_MS);
}

static int film_read_exact(FILE *file, uint32_t offset, void *dst, uint32_t count)
{
    uint32_t done = 0;
    if (FIO_SeekSkipFile(file, offset, SEEK_SET) < 0) return -1;
    while (done < count) {
        uint32_t n = count - done;
        int got;
        if (n > FILM_IO) n = FILM_IO;
        got = FIO_ReadFile(file, (char *)dst + done, n);
        if (got <= 0 || (uint32_t)got > n) return -1;
        done += (uint32_t)got;
    }
    return 0;
}

static int film_read_preview(void *ctx, uint32_t offset, void *dst, uint32_t count)
{
    film_job *job = ctx;
    if (film_abort(job) || offset > job->preview_length ||
        count > job->preview_length - offset || count > FILM_IO) return -1;
    if (film_read_exact(job->source, job->preview_offset + offset, job->io, count)) return -1;
    memcpy(dst, job->io, count);
    return 0;
}

static int film_write_jpeg(void *ctx, const uint8_t *data, size_t count)
{
    film_job *job = ctx;
    if (film_abort(job) || count > FILM_MAX_OUTPUT - job->written) return -1;
    while (count) {
        size_t n = count > FILM_IO ? FILM_IO : count;
        memcpy(job->io, data, n);
        if (FIO_WriteFile(job->output, job->io, n) != (int)n) return -1;
        job->written += n;
        data += n;
        count -= n;
    }
    return 0;
}

static int film_encode_row(void *ctx, const uint8_t *row, int width)
{
    film_job *job = ctx;
    if (film_abort(job) || width != job->width || job->rows >= job->height) return -1;
    if (se_jpeg_write_rows(job->encoder, row, 1) != SE_OK) return -1;
    job->rows++;
    /* Cooperatively yield without using sensor, display or capture hooks. */
    if (!(job->rows & 15)) msleep(1);
    return 0;
}

static int film_effect_row(void *ctx, const uint8_t *row, int width)
{
    film_job *job = ctx;
    if (film_abort(job) || width != job->width) return -1;
    return se_anamorphic_rows(job->effect, row, 1, film_encode_row, job) == SE_OK ? 0 : -1;
}

static int film_decode_rows(void *ctx, const uint8_t *rgb, int rows, int width)
{
    film_job *job = ctx;
    if (film_abort(job) || width != job->width) return -1;
    return se_look_rows(job->look, rgb, rows, film_effect_row, job) == SE_OK ? 0 : -1;
}

static int film_load_stock(const char *filename, se_stock *stock)
{
    char path[64];
    char *body;
    int got, ok;
    if (!filename[0]) return -1;
    body = fio_malloc(16384);
    if (!body) return -1;
    snprintf(path, sizeof(path), "ML/FILM/%s", filename);
    got = (int)read_file(path, body, 16383);
    se_stock_init(stock);
    ok = got > 0 && got < 16383 && se_stock_parse(body, got, stock) == SE_OK;
    fio_free(body);
    return ok ? 0 : -1;
}

static int film_verify_output(const char *path, uint32_t expected)
{
    FILE *file;
    uint32_t size;
    uint8_t bytes[2];
    int ok = 0;
    if (expected < 4 || FIO_GetFileSize(path, &size) || size != expected) return -1;
    file = FIO_OpenFile(path, O_RDONLY);
    if (!file) return -1;
    if (!film_read_exact(file, 0, bytes, 2) && bytes[0] == 0xff && bytes[1] == 0xd8 &&
        !film_read_exact(file, size - 2, bytes, 2) && bytes[0] == 0xff && bytes[1] == 0xd9)
        ok = 1;
    FIO_CloseFile(file);
    return ok ? 0 : -1;
}

static void film_saved_status(uint32_t started)
{
    char message[35];
    uint32_t elapsed_ms = (uint32_t)get_ms_clock() - started;
    /* Native formatting supports integer widths, not floating point.
     * Even UINT32_MAX milliseconds fits the 34-character value limit. */
    snprintf(message, sizeof(message), "JPEG saved %d.%03ds; CR2 safe",
             (int)(elapsed_ms / 1000u), (int)(elapsed_ms % 1000u));
    film_t6_set_status(message);
}

/* No historical directory walk: callers supply one bounded, current path. */
static int film_develop(const char *source, const char *destination, const char *temporary,
                        const struct film_t6_settings *settings, int automatic)
{
    film_job job;
    se_stock *stock = NULL;
    se_look *look = NULL;
    se_cr2_info *metadata = NULL;
    se_anamorphic effect;
    se_jdec_info jpeg;
    se_jpeg_exif exif;
    se_arena decode_arena, look_arena, effect_arena;
    uint8_t *header = NULL, *memory = NULL, *strip;
    uint32_t source_size = 0, end_size = 0;
    int got, preview_header, pw, ph, width, height, rc = -1, created = 0;
    size_t look_bytes, effect_bytes;

    memset(&job, 0, sizeof(job));
    job.started = (uint32_t)get_ms_clock();
    job.automatic = automatic;
    /* Existing files, including previous TMPs, belong to the user. */
    if (is_file(destination) || is_file(temporary)) {
        film_t6_set_status("Output exists; left untouched");
        return 1;
    }
    film_t6_set_processing(1);
    if (FIO_GetFileSize(source, &source_size) || !source_size || source_size > FILM_MAX_INPUT)
        goto done;
    msleep(600);
    if (film_abort(&job) || FIO_GetFileSize(source, &end_size) || end_size != source_size) goto done;
    job.source = FIO_OpenFile(source, O_RDONLY);
    header = fio_malloc(FILM_HEADER);
    job.io = fio_malloc(FILM_IO);
    stock = malloc(sizeof(*stock));
    look = malloc(sizeof(*look));
    metadata = malloc(sizeof(*metadata));
    if (!job.source || !header || !job.io || !stock || !look || !metadata) goto done;
    got = source_size < FILM_HEADER ? source_size : FILM_HEADER;
    if (film_read_exact(job.source, 0, header, got)) goto done;
    if (se_cr2_find_preview(header, got, &job.preview_offset, &job.preview_length, &pw, &ph) != SE_OK ||
        job.preview_offset >= (uint32_t)got || job.preview_offset > source_size ||
        !job.preview_length || job.preview_length > FILM_MAX_PREVIEW ||
        job.preview_length > source_size - job.preview_offset) goto done;
    preview_header = got - job.preview_offset;
    if ((uint32_t)preview_header > job.preview_length) preview_header = job.preview_length;
    if (se_jpeg_dec_info(header + job.preview_offset, preview_header, &jpeg) != SE_OK ||
        jpeg.width < 16 || jpeg.height < 16 || jpeg.width > 6000 || jpeg.height > 4000) goto done;
    if (se_cr2_parse(header, got, metadata) != SE_OK) memset(metadata, 0, sizeof(*metadata));
    if (film_load_stock(settings->stock, stock)) goto done;
    se_jpeg_scaled_size(jpeg.width, jpeg.height, FILM_SCALE, &width, &height);
    job.width = width;
    job.height = height;
    se_look_from_stock(stock, FILM_SCALE, look);
    look_bytes = se_look_fit(look, width, FILM_LOOK);
    se_anamorphic_preset(&effect, settings->effect, settings->amber, width);
    effect_bytes = se_anamorphic_fit(&effect, width, FILM_EFFECT);
    if (!look_bytes || look_bytes > FILM_LOOK || !effect_bytes || effect_bytes > FILM_EFFECT) goto done;
    memory = malloc(FILM_DECODE + look_bytes + effect_bytes + 64);
    if (!memory) goto done;
    se_arena_init(&decode_arena, memory, FILM_DECODE);
    se_arena_init(&look_arena, memory + FILM_DECODE, look_bytes);
    se_arena_init(&effect_arena, memory + FILM_DECODE + look_bytes, effect_bytes);
    job.encoder = SE_NEW(&decode_arena, se_jpeg, 1);
    strip = SE_NEW(&decode_arena, uint8_t, (size_t)8 * width * 3);
    if (!job.encoder || !strip ||
        se_look_begin(&job.look, look, width, &look_arena) != SE_OK ||
        se_anamorphic_begin(&job.effect, &effect, width, height, &effect_arena) != SE_OK) goto done;
    if (film_abort(&job) || is_file(temporary) || is_file(destination)) goto done;
    /* ML's FIO_CreateFile first removes an existing path. Never use it here.
     * Ask this T6 firmware for exclusive creation, including its required
     * existing-name lookup, and abort if it rejects the native flags;
     * there is deliberately no create/truncate/remove-and-retry fallback.
     * New-file creation also completed on the physical T6. Late-collision
     * preservation is emulator-tested and still needs physical validation. */
    job.output = FIO_OpenFile(temporary, FILM_T6_CREATE_NEW);
    if (!job.output) goto done;
    created = 1;
    memset(&exif, 0, sizeof(exif));
    exif.make = metadata->make[0] ? metadata->make : "Canon";
    exif.model = metadata->model[0] ? metadata->model : "Canon EOS 1300D";
    exif.software = "T6 Cinema experimental";
    exif.datetime = metadata->datetime;
    exif.exposure_time = metadata->exposure_time;
    exif.aperture = metadata->aperture;
    exif.iso = metadata->iso;
    film_t6_set_status("Developing separate JPEG...");
    if (se_jpeg_begin(job.encoder, width, height, 88, strip, &exif, film_write_jpeg, &job) != SE_OK)
        goto done;
    if (se_jpeg_decode_stream_scaled(header + job.preview_offset, preview_header,
            job.preview_length, film_read_preview, &job, FILM_SCALE, &decode_arena,
            film_decode_rows, &job, &jpeg) != SE_OK ||
        se_look_end(job.look, film_effect_row, &job) != SE_OK ||
        se_anamorphic_end(job.effect, film_encode_row, &job) != SE_OK ||
        job.rows != height || se_jpeg_end(job.encoder) != SE_OK || film_abort(&job)) goto done;
    FIO_CloseFile(job.output);
    job.output = NULL;
    FIO_CloseFile(job.source);
    job.source = NULL;
    if (FIO_GetFileSize(source, &end_size) || end_size != source_size ||
        film_verify_output(temporary, job.written) || is_file(destination)) goto done;
    if (FIO_RenameFile(temporary, destination) || film_verify_output(destination, job.written)) goto done;
    created = 0;
    rc = 0;
done:
    if (job.output) FIO_CloseFile(job.output);
    if (job.source) FIO_CloseFile(job.source);
    /* This flag is only set after this invocation creates this exact TMP. */
    if (created) FIO_RemoveFile(temporary);
    if (memory) free(memory);
    if (header) fio_free(header);
    if (job.io) fio_free(job.io);
    if (stock) free(stock);
    if (look) free(look);
    if (metadata) free(metadata);
    film_t6_set_processing(0);
    if (rc == 0) {
        film_saved_status(job.started);
    } else {
        film_t6_set_status(film_t6_cancelled() ?
            "Cancelled; CR2 untouched" : "Failed; CR2 untouched");
    }
    return rc;
}

static void film_one(const char *directory, const char *prefix, int number, int automatic)
{
    char source[FILM_T6_PATH], destination[FILM_T6_PATH], temporary[FILM_T6_PATH];
    struct film_t6_settings settings;
    int tries;
    snprintf(source, sizeof(source), "%s/%s%04d.CR2", directory, prefix, number);
    snprintf(destination, sizeof(destination), "%s/T6F_%04d.JPG", directory, number);
    snprintf(temporary, sizeof(temporary), "%s/T6F_%04d.TMP", directory, number);
    film_t6_snapshot(&settings);
    for (tries = 0; tries < 5 && !is_file(source) && !film_t6_cancelled(); tries++) msleep(250);
    if (!is_file(source)) { film_t6_set_status("No latest CR2; shoot RAW"); return; }
    film_develop(source, destination, temporary, &settings, automatic);
}

static void film_worker(void *unused)
{
    int baseline = -1, previous_auto = 0;
    char previous_directory[FILM_T6_PATH] = "";
    (void)unused;
    while (!ml_started && !ml_shutdown_requested) msleep(100);
    if (ml_shutdown_requested) return;
    msleep(1000);
    film_t6_load_stocks();
    while (!ml_shutdown_requested) {
        struct card_info *card = get_shooting_card();
        char directory[FILM_T6_PATH], prefix[8];
        int automatic = film_t6_auto_enabled();
        int manual;
        int current = card ? card->file_number : 0;
        if (film_camera_busy()) { msleep(500); continue; }
        if (film_t6_bench_pending()) {
            if (!lv && film_t6_take_bench_request()) {
                /* The queue action cleared stale cancellation. Never clear it
                 * here: Stop may arrive after claim but before this call. */
                film_t6_set_processing(1);
                film_cpu_bench();
                film_t6_set_processing(0);
                previous_auto = 0; /* A later Auto enable starts a new baseline. */
            }
            msleep(500);
            continue;
        }
        manual = film_t6_take_request();
        if (!card || current < 1 || current > 9999) { msleep(500); continue; }
        snprintf(directory, sizeof(directory), "%s", get_dcim_dir());
        snprintf(prefix, sizeof(prefix), "%s", get_file_prefix());
        if (strlen(prefix) != 4 || !directory[0]) { msleep(500); continue; }
        if (manual) {
            film_t6_clear_cancel();
            film_one(directory, prefix, current, 0);
        }
        if (!automatic || !previous_auto || strcmp(directory, previous_directory) || current < baseline) {
            baseline = current; /* Enabling never walks old photos. */
        } else if (current > baseline && !manual) {
            int next = baseline + 1;
            int last = current;
            if (last - next + 1 > FILM_MAX_BATCH) {
                next = last - FILM_MAX_BATCH + 1;
                film_t6_set_status("Burst capped at newest four CR2s");
            }
            film_t6_clear_cancel();
            for (; next <= last && film_t6_auto_enabled() && !film_t6_cancelled(); next++)
                film_one(directory, prefix, next, 1);
            baseline = last; /* Failures remain unchanged; retry manually. */
        }
        if (manual) baseline = current;
        previous_auto = automatic;
        snprintf(previous_directory, sizeof(previous_directory), "%s", directory);
        msleep(500);
    }
}
TASK_CREATE("t6film", film_worker, 0, 0x1f, 0x10000);
