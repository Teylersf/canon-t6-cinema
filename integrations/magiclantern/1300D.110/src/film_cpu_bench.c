/* T6 Cinema offscreen synthetic CPU test. GPL-2.0-only.
 * Only fio_malloc-owned pixels are processed. No camera frame addresses,
 * encoder/capture hooks, sensor changes or timer writes are used here.
 */
#include "dryos.h"
#include "mem.h"
#include "fio-ml.h"
#include "lens.h"
#include "propvalues.h"
#include "film_t6.h"
#include "film_cpu_bench.h"
#include "yuv_anamorphic.h"

#if !defined(CONFIG_1300D_110)
#error Synthetic CPU test is scoped to 1300D firmware 1.1.0
#endif

#define CPU_PIXELS_BYTES (640u * 480u * 2u)
#define CPU_SCRATCH_BYTES (640u * 2u)
#define CPU_ALLOCATION_BYTES (CPU_PIXELS_BYTES + CPU_SCRATCH_BYTES)
#define CPU_BUDGET_MS 10000u
#define CPU_REPORT_BYTES 2048u
#define CPU_VERIFY_BYTES 64u
#define CPU_REPORT_SLOTS 16
/* 1300D.110 native write/create/exclusive/check-existing, with NO truncate.
 * These differ from ML's global O_CREAT/O_EXCL; no destructive fallback. */
#define CPU_T6_CREATE_NEW 0x8501u

static struct film_cpu_results cpu_results;
static char cpu_report[CPU_REPORT_BYTES];

const struct film_cpu_results *film_cpu_bench_get_results(void)
{
    return &cpu_results;
}

static int cpu_unavailable(void)
{
    if (ml_shutdown_requested || film_t6_cancelled()) {
        cpu_results.state = FILM_CPU_CANCELLED;
        film_t6_set_status("CPU test canceled; no report");
        return 1;
    }
    if (lens_info.job_state || get_halfshutter_pressed() || lv || is_movie_mode()) {
        cpu_results.state = FILM_CPU_BUSY;
        film_t6_set_status("CPU test stopped for camera");
        return 1;
    }
    return 0;
}

static int cpu_interrupted(uint32_t started)
{
    if (cpu_unavailable()) return 1;
    if ((uint32_t)((uint32_t)get_ms_clock() - started) >= CPU_BUDGET_MS) {
        cpu_results.state = FILM_CPU_DEADLINE;
        film_t6_set_status("CPU test: cooperative 10s budget");
        return 1;
    }
    return 0;
}

/* Deterministic generic studio-range YUYV, with highlights on every row.
 * This is generated outside every timed interval; it contains no camera data. */
static void cpu_fill(uint8_t *pixels, unsigned width, unsigned height, unsigned seed)
{
    unsigned y, x;
    for (y = 0; y < height; ++y) {
        uint8_t *row = pixels + (size_t)y * width * 2u;
        unsigned first = ((width / 4u) + (y & 15u)) & ~1u;
        unsigned second = ((width * 3u) / 4u) & ~1u;
        for (x = 0; x < width; x += 2u) {
            size_t offset = (size_t)x * 2u;
            row[offset] = (uint8_t)(16u + ((x + y * 3u + seed * 13u) & 63u));
            row[offset + 1u] = 128u;
            row[offset + 2u] = (uint8_t)(16u + ((x * 3u + y + seed * 7u) & 63u));
            row[offset + 3u] = 128u;
            if (x == first || x == second) {
                row[offset] = 235u;
                row[offset + 2u] = 230u;
            }
        }
    }
}

typedef struct {
    size_t used;
    int failed;
} cpu_csv;

static void csv_text(cpu_csv *csv, const char *text)
{
    while (*text && !csv->failed) {
        if (csv->used >= sizeof(cpu_report) - 1u) {
            csv->failed = 1;
            break;
        }
        cpu_report[csv->used++] = *text++;
    }
    cpu_report[csv->used] = 0;
}

static void csv_number(cpu_csv *csv, uint32_t value)
{
    char reverse[10], one[2];
    unsigned count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    one[1] = 0;
    while (count) {
        one[0] = reverse[--count];
        csv_text(csv, one);
    }
}

static void csv_field(cpu_csv *csv, uint32_t value)
{
    csv_text(csv, ",");
    csv_number(csv, value);
}

static size_t cpu_make_report(void)
{
    cpu_csv csv = {0, 0};
    int c, run;
    cpu_report[0] = 0;
    csv_text(&csv, cpu_results.state == FILM_CPU_DEADLINE ?
             "status,deadline\n" : "status,complete\n");
    csv_text(&csv,
        "type,synthetic_cpu_kernel_only\n"
        "version,1\n"
        "kernel,se_ya_apply\n"
        "source,owned_synthetic\n"
        "layout,YUYV\n"
        "range,studio_Y16_235_C16_240\n"
        "chroma,unsigned128\n"
        "preset,strong\n"
        "tint,blue\n"
        "timer,get_ms_clock\n"
        "unit,ms_raw\n"
        "warmup_per_case_requested,1\n"
        "timed_per_case_requested,3\n"
        "fills_timed,0\n"
        "allocation_api,fio_malloc\n"
        "frame_allocation_bytes,614400\n"
        "scratch_allocation_bytes,1280\n"
        "cooperative_budget_ms,10000\n"
        "hard_deadline,0\n"
        "encoder_measured,0\n"
        "recording_fps_measured,0\n"
        "unique_exposures_measured,0\n"
        "actual_camera_format_known,0\n"
        "elapsed_before_file_io_ms,");
    csv_number(&csv, cpu_results.elapsed_ms);
    csv_text(&csv, "\ncase,width,height,warmup,completed_frames,ms1,ms2,ms3,min_ms,max_ms\n");
    for (c = 0; c < FILM_CPU_CASES; ++c) {
        const struct film_cpu_case *result = &cpu_results.cases[c];
        csv_number(&csv, (uint32_t)c);
        csv_field(&csv, result->width);
        csv_field(&csv, result->height);
        csv_field(&csv, result->warmup_done);
        csv_field(&csv, result->count);
        for (run = 0; run < FILM_CPU_RUNS; ++run) {
            if ((uint32_t)run < result->count) csv_field(&csv, result->milliseconds[run]);
            else csv_text(&csv, ",-1");
        }
        if (result->count) {
            csv_field(&csv, result->minimum_ms);
            csv_field(&csv, result->maximum_ms);
        } else csv_text(&csv, ",-1,-1");
        csv_text(&csv, "\n");
    }
    return csv.failed ? 0u : csv.used;
}

/* A checked DMA-capable allocation avoids FIO_ReadFile's cached-buffer path,
 * which allocates its own bounce buffer and asserts if that allocation fails.
 * Require byte-for-byte roundtrip plus unchanged length before publication. */
static int cpu_verify_report(const char *path, size_t length)
{
    FILE *file = NULL;
    uint32_t size;
    uint8_t *buffer;
    size_t done = 0;
    int ok = 0;
    if (!length || length >= sizeof(cpu_report) ||
        FIO_GetFileSize(path, &size) || size != length) return 0;
    buffer = fio_malloc(CPU_VERIFY_BYTES);
    if (!buffer) return 0;
    file = FIO_OpenFile(path, O_RDONLY);
    if (!file) goto done;
    while (done < length) {
        size_t amount = length - done;
        int got;
        if (amount > CPU_VERIFY_BYTES) amount = CPU_VERIFY_BYTES;
        got = FIO_ReadFile(file, buffer, amount);
        if (got <= 0 || (size_t)got > amount ||
            memcmp(buffer, cpu_report + done, (size_t)got)) goto done;
        done += (size_t)got;
    }
    ok = !FIO_GetFileSize(path, &size) && size == length;
done:
    if (file) FIO_CloseFile(file);
    fio_free(buffer);
    return ok;
}

/* The large allocation is already freed before calling this function.
 * Only the TMP successfully created by this invocation may be removed. */
static int cpu_save_report(void)
{
    char destination[32], temporary[32], status[48];
    FILE *output = NULL;
    size_t length = cpu_make_report();
    int slot, created = 0, rc = -1;
    int partial = cpu_results.state == FILM_CPU_DEADLINE;
    if (!length) goto error;
    /* Deadline stops further kernels. Once the buffer is freed, partial
     * measurements may be saved only if cancellation/camera guards allow it. */
    if (cpu_unavailable()) return 1;
    for (slot = 1; slot <= CPU_REPORT_SLOTS; ++slot) {
        snprintf(destination, sizeof(destination), "ML/FILM/CPUB%04d.CSV", slot);
        snprintf(temporary, sizeof(temporary), "ML/FILM/CPUB%04d.TMP", slot);
        if (strlen(destination) != 20u || strlen(temporary) != 20u) goto error;
        if (!is_file(destination) && !is_file(temporary)) break;
        if (cpu_unavailable()) return 1;
    }
    if (slot > CPU_REPORT_SLOTS) goto error;
    if (cpu_unavailable()) return 1;
    if (is_file(destination) || is_file(temporary)) goto error;
    output = FIO_OpenFile(temporary, CPU_T6_CREATE_NEW);
    if (!output) goto error;
    created = 1;
    if (cpu_unavailable()) { rc = 1; goto done; }
    if (FIO_WriteFile(output, cpu_report, length) != (int)length) goto error;
    FIO_CloseFile(output);
    output = NULL;
    if (cpu_unavailable()) { rc = 1; goto done; }
    if (!cpu_verify_report(temporary, length)) goto error;
    if (cpu_unavailable()) { rc = 1; goto done; }
    if (is_file(destination) || FIO_RenameFile(temporary, destination)) goto error;
    created = 0;
    /* Rename is the publication boundary. Do not remove a completed CSV if a
     * later verification fails, and never delete another invocation's file. */
    if (!cpu_verify_report(destination, length)) goto error;
    memcpy(cpu_results.report_path, destination, strlen(destination) + 1u);
    cpu_results.state = partial ? FILM_CPU_DEADLINE : FILM_CPU_SAVED;
    snprintf(status, sizeof(status), partial ? "CPU partial: CPUB%04d.CSV" :
             "CPU report: CPUB%04d.CSV", slot);
    film_t6_set_status(status);
    return 0;
error:
    cpu_results.state = FILM_CPU_REPORT_ERROR;
    film_t6_set_status("CPU test: report not saved/verified");
done:
    if (output) FIO_CloseFile(output);
    if (created) FIO_RemoveFile(temporary);
    return rc;
}

int film_cpu_bench(void)
{
    static const unsigned widths[FILM_CPU_CASES] = {320u, 640u, 640u};
    static const unsigned heights[FILM_CPU_CASES] = {180u, 360u, 480u};
    uint8_t *memory = NULL;
    uint32_t started = (uint32_t)get_ms_clock();
    se_ya_params params;
    se_ya_frame frame;
    int c, run, rc = -1;
    char status[48];
    memset(&cpu_results, 0, sizeof(cpu_results));
    cpu_results.version = 1;
    cpu_results.state = FILM_CPU_RUNNING;
    for (c = 0; c < FILM_CPU_CASES; ++c) {
        cpu_results.cases[c].width = widths[c];
        cpu_results.cases[c].height = heights[c];
    }
    if (cpu_interrupted(started)) { rc = 1; goto done; }
    memory = fio_malloc(CPU_ALLOCATION_BYTES);
    if (!memory) {
        cpu_results.state = FILM_CPU_NO_MEMORY;
        film_t6_set_status("CPU test: insufficient memory");
        goto done;
    }
    if (se_ya_preset(&params, SE_YA_STUDIO_RANGE, 3u, 0u) != SE_YA_OK) {
        cpu_results.state = FILM_CPU_KERNEL_ERROR;
        film_t6_set_status("CPU test: invalid synthetic recipe");
        goto done;
    }
    frame.data = memory;
    frame.bytes = CPU_PIXELS_BYTES;
    frame.layout = SE_YA_YUYV;
    frame.range = SE_YA_STUDIO_RANGE;
    frame.chroma = SE_YA_CHROMA_UNSIGNED_128;
    frame.ownership = SE_YA_EXCLUSIVE_CPU;
    for (c = 0; c < FILM_CPU_CASES; ++c) {
        struct film_cpu_case *result = &cpu_results.cases[c];
        frame.width = widths[c];
        frame.height = heights[c];
        frame.stride = (size_t)frame.width * 2u;
        snprintf(status, sizeof(status), "CPU test %d/%d: synthetic", c + 1, FILM_CPU_CASES);
        film_t6_set_status(status);
        for (run = 0; run <= FILM_CPU_RUNS; ++run) {
            uint32_t before, after, elapsed;
            se_ya_result result_code;
            if (cpu_interrupted(started)) { rc = 1; goto done; }
            cpu_fill(memory, frame.width, frame.height,
                     (unsigned)(c * (FILM_CPU_RUNS + 1) + run));
            if (cpu_interrupted(started)) { rc = 1; goto done; }
            before = (uint32_t)get_ms_clock();
            result_code = se_ya_apply(&frame, &params, memory + CPU_PIXELS_BYTES,
                                     CPU_SCRATCH_BYTES);
            after = (uint32_t)get_ms_clock();
            elapsed = (uint32_t)(after - before);
            if (result_code != SE_YA_OK) {
                cpu_results.state = FILM_CPU_KERNEL_ERROR;
                film_t6_set_status("CPU test: synthetic kernel failed");
                goto done;
            }
            if (!run) result->warmup_done = 1;
            else {
                unsigned index = result->count;
                result->milliseconds[index] = elapsed;
                if (!index || elapsed < result->minimum_ms) result->minimum_ms = elapsed;
                if (!index || elapsed > result->maximum_ms) result->maximum_ms = elapsed;
                result->count++;
            }
            if (cpu_interrupted(started)) { rc = 1; goto done; }
            msleep(1);
            if (cpu_interrupted(started)) { rc = 1; goto done; }
        }
    }
    rc = 0;
done:
    cpu_results.elapsed_ms = (uint32_t)((uint32_t)get_ms_clock() - started);
    cpu_results.cases_completed = 0;
    for (c = 0; c < FILM_CPU_CASES; ++c)
        if (cpu_results.cases[c].warmup_done && cpu_results.cases[c].count == FILM_CPU_RUNS)
            cpu_results.cases_completed++;
    if (memory) fio_free(memory);
    if (rc && cpu_results.state != FILM_CPU_DEADLINE) return rc;
    return cpu_save_report();
}
