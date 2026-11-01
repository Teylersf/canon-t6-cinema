/* T6 manual synthetic HEVC proof. GPL-2.0-only. No capture source or hooks. */
#include "dryos.h"
#include "mem.h"
#include "fio-ml.h"
#include "lens.h"
#include "propvalues.h"
#include "film_t6.h"
#include "film_hevc.h"
#include "hevc_intra.h"

#if !defined(CONFIG_1300D_110)
#error Synthetic HEVC proof is scoped to 1300D firmware 1.1.0
#endif

#define HEVC_T6_CREATE_NEW 0x8501u /* native write/create/exclusive/check-existing; NO truncate */
#define HEVC_SLOTS 16

static struct film_hevc_results hevc_results;
static const uint32_t hevc_crc_table[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu, 0xe963a535u, 0x9e6495a3u,
    0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u, 0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u,
    0x1db71064u, 0x6ab020f2u, 0xf3b97148u, 0x84be41deu, 0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
    0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u, 0xfa0f3d63u, 0x8d080df5u,
    0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u, 0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu,
    0x35b5a8fau, 0x42b2986cu, 0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
    0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u, 0xcfba9599u, 0xb8bda50fu,
    0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u, 0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du,
    0x76dc4190u, 0x01db7106u, 0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
    0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du, 0x91646c97u, 0xe6635c01u,
    0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu, 0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u,
    0x65b0d9c6u, 0x12b7e950u, 0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
    0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u, 0xa4d1c46du, 0xd3d6f4fbu,
    0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u, 0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u,
    0x5005713cu, 0x270241aau, 0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
    0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u, 0xb7bd5c3bu, 0xc0ba6cadu,
    0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au, 0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u,
    0xe3630b12u, 0x94643b84u, 0x0d6d6a3eu, 0x7a6a5aa8u, 0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
    0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu, 0x196c3671u, 0x6e6b06e7u,
    0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu, 0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u,
    0xd6d6a3e8u, 0xa1d1937eu, 0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u, 0x316e8eefu, 0x4669be79u,
    0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u, 0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu,
    0xc5ba3bbeu, 0xb2bd0b28u, 0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
    0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au, 0x9c0906a9u, 0xeb0e363fu, 0x72076785u, 0x05005713u,
    0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u, 0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u,
    0x86d3d2d4u, 0xf1d4e242u, 0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
    0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u, 0x616bffd3u, 0x166ccf45u,
    0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u, 0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu,
    0xaed16a4au, 0xd9d65adcu, 0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
    0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u, 0xcdd70693u, 0x54de5729u, 0x23d967bfu,
    0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u, 0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du,
};

typedef struct {
    FILE *output;
    uint8_t *dma;
    uint32_t started, budget, written, accepted, copied, crc, source_ctus, pending;
    enum film_hevc_phase phase;
    uint32_t phase_started;
} hevc_job;

const struct film_hevc_results *film_hevc_get_results(void) { return &hevc_results; }

uint8_t film_hevc_sample(unsigned frame, unsigned plane, uint32_t x, uint32_t y)
{
    if (plane == 0u) return (uint8_t)(3u*x + 5u*y + 29u*frame);
    if (plane == 1u) return (uint8_t)(64u + 7u*x + 3u*y + 11u*frame);
    return (uint8_t)(192u + 2u*x + 9u*y + 13u*frame);
}

static uint32_t hevc_crc(uint32_t crc, const uint8_t *data, size_t bytes)
{
    size_t i;
    for (i = 0; i < bytes; ++i) crc = hevc_crc_table[(crc ^ data[i]) & 255u] ^ (crc >> 8);
    return crc;
}

static int hevc_interrupted(hevc_job *job)
{
    if (film_t6_cancelled() || ml_shutdown_requested) {
        hevc_results.state = FILM_HEVC_CANCELLED;
        return 1;
    }
    if (lens_info.job_state || get_halfshutter_pressed() || lv || is_movie_mode() || RECORDING) {
        hevc_results.state = FILM_HEVC_BUSY;
        return 1;
    }
    if ((uint32_t)((uint32_t)get_ms_clock() - job->started) >= job->budget) {
        hevc_results.state = FILM_HEVC_DEADLINE;
        return 1;
    }
    return 0;
}

static int hevc_cancelled(void *context) { return hevc_interrupted(context); }

static void hevc_phase_begin(hevc_job *job, enum film_hevc_phase phase)
{
    job->phase = phase;
    job->phase_started = (uint32_t)get_ms_clock();
    hevc_results.phase_started_mask |= 1u << ((unsigned)phase - 1u);
}

static void hevc_phase_end(hevc_job *job, int complete)
{
    unsigned index;
    if (job->phase == FILM_HEVC_PHASE_NONE) return;
    index = (unsigned)job->phase - 1u;
    hevc_results.phase_ms[index] += (uint32_t)((uint32_t)get_ms_clock() - job->phase_started);
    if (complete) hevc_results.phase_completed_mask |= 1u << index;
    else hevc_results.stopped_phase = (uint32_t)job->phase;
    job->phase = FILM_HEVC_PHASE_NONE;
}

static int hevc_read_ctu(void *context, uint32_t frame, uint32_t x, uint32_t y,
                         uint8_t block[HI_CTU_BYTES])
{
    hevc_job *job = context;
    uint32_t plane, row, column;
    if (hevc_interrupted(job)) return 0;
    for (plane = 0; plane < 3u; ++plane) {
        uint32_t side = plane ? 16u : 32u;
        uint32_t px = plane ? x/2u : x, py = plane ? y/2u : y;
        uint32_t step = plane == 0u ? 3u : plane == 1u ? 7u : 2u;
        uint32_t offset = plane == 0u ? 0u : plane == 1u ? 1024u : 1280u;
        for (row = 0; row < side; ++row) {
            uint8_t value = film_hevc_sample(frame, plane, px, py + row);
            for (column = 0; column < side; ++column) {
                block[offset + row*side + column] = value;
                value = (uint8_t)(value + step);
            }
        }
    }
    /* Low-priority worker yields cooperatively without requesting capture. */
    if (!(++job->source_ctus & 63u)) {
        msleep(1);
        if (hevc_interrupted(job)) return 0;
    }
    return 1;
}

static int hevc_flush(hevc_job *job)
{
    int wrote;
    uint32_t pending = job->pending;
    if (!pending) return 1;
    if (hevc_interrupted(job)) return 0;
    ++hevc_results.fio_write_calls;
    wrote = FIO_WriteFile(job->output, job->dma, pending);
    if (wrote > 0 && (uint32_t)wrote <= pending) {
        job->written += (uint32_t)wrote;
        job->crc = hevc_crc(job->crc, job->dma, (size_t)wrote);
    }
    if (wrote < 0 || (uint32_t)wrote != pending) {
        hevc_results.state = FILM_HEVC_IO_ERROR;
        return 0; /* Never retry or flush a failed stream during cleanup. */
    }
    job->pending = 0;
    if (hevc_interrupted(job)) return 0;
    return 1;
}

static int hevc_write(void *context, const uint8_t *data, size_t bytes)
{
    hevc_job *job = context;
    size_t remaining = bytes;
    ++hevc_results.write_callback_calls;
    if (hevc_interrupted(job)) return 0;
    if (bytes > HI_OUTPUT_BYTES || bytes > FILM_HEVC_OUTPUT_LIMIT - job->copied) {
        hevc_results.state = FILM_HEVC_CODEC_ERROR;
        return 0;
    }
    while (remaining) {
        size_t amount = FILM_HEVC_DMA_BYTES - job->pending;
        if (amount > remaining) amount = remaining;
        memcpy(job->dma + job->pending, data, amount);
        job->pending += (uint32_t)amount;
        job->copied += (uint32_t)amount;
        data += amount;
        remaining -= amount;
        if (job->pending == FILM_HEVC_DMA_BYTES && !hevc_flush(job)) return 0;
    }
    job->accepted += (uint32_t)bytes;
    return 1;
}

/* Only the explicit uncached fio_malloc buffer reaches FIO_ReadFile. Cached
 * stack reads invoke an allocating/asserting fallback in pinned ML. */
static int hevc_verify(hevc_job *job, const char *path, uint8_t *buffer)
{
    FILE *input = NULL;
    uint32_t size, done = 0, crc = 0xffffffffu;
    int ok = 0;
    if (hevc_interrupted(job) || FIO_GetFileSize(path, &size) || size != job->written) return 0;
    if (hevc_interrupted(job)) return 0;
    input = FIO_OpenFile(path, O_RDONLY);
    if (!input) return 0;
    while (done < job->written) {
        uint32_t amount = job->written - done;
        int got;
        if (hevc_interrupted(job)) goto done;
        if (amount > FILM_HEVC_DMA_BYTES) amount = FILM_HEVC_DMA_BYTES;
        ++hevc_results.fio_read_calls;
        got = FIO_ReadFile(input, buffer, amount);
        if (got <= 0 || (uint32_t)got > amount) goto done;
        hevc_results.fio_read_bytes += (uint32_t)got;
        crc = hevc_crc(crc, buffer, (size_t)got);
        done += (uint32_t)got;
    }
    if (hevc_interrupted(job)) goto done;
    ok = !FIO_GetFileSize(path, &size) && size == job->written && crc == job->crc;
done:
    FIO_CloseFile(input);
    return ok;
}

typedef struct {
    uint32_t started;
    int io_started;
} hevc_report_job;

typedef struct { size_t used; int failed; } hevc_csv;
static char hevc_report_body[FILM_HEVC_REPORT_MAX_BYTES];

/* This guard deliberately does NOT call hevc_interrupted: the proof's budget
 * may already have expired, and its frozen outcome must never be overwritten. */
static int hevc_report_interrupted(hevc_report_job *report)
{
    if (film_t6_cancelled() || ml_shutdown_requested || lens_info.job_state ||
        get_halfshutter_pressed() || lv || is_movie_mode() || RECORDING) {
        hevc_results.report_state = report->io_started ? FILM_HEVC_REPORT_INTERRUPTED :
                                                        FILM_HEVC_REPORT_SKIPPED_GUARD;
        return 1;
    }
    if ((uint32_t)((uint32_t)get_ms_clock() - report->started) >= FILM_HEVC_REPORT_DEADLINE_MS) {
        hevc_results.report_state = FILM_HEVC_REPORT_DEADLINE;
        return 1;
    }
    return 0;
}

static void hevc_csv_text(hevc_csv *csv, const char *text)
{
    while (*text && !csv->failed) {
        if (csv->used >= sizeof(hevc_report_body) - 1u) { csv->failed = 1; break; }
        hevc_report_body[csv->used++] = *text++;
    }
    hevc_report_body[csv->used] = 0;
}

static void hevc_csv_number(hevc_csv *csv, uint32_t value)
{
    char reverse[10], digit[2];
    unsigned count = 0;
    do { reverse[count++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    digit[1] = 0;
    while (count) { digit[0] = reverse[--count]; hevc_csv_text(csv, digit); }
}

static void hevc_csv_value(hevc_csv *csv, const char *key, uint32_t value)
{
    hevc_csv_text(csv, key); hevc_csv_text(csv, ",");
    hevc_csv_number(csv, value); hevc_csv_text(csv, "\n");
}

static size_t hevc_make_report(const struct film_hevc_results *proof, const char *attempt_path)
{
    static const char *const phase_names[FILM_HEVC_PHASE_COUNT] = {
        "setup_ms", "encode_ms", "tmp_verify_ms", "rename_ms", "final_verify_ms"
    };
    static const char *const state_names[] = {
        "idle", "running", "saved", "cancelled", "busy", "deadline", "no_memory",
        "bad_size", "io_error", "verify_error", "codec_error", "memory_class_error"
    };
    hevc_csv csv = {0, 0};
    unsigned i;
    hevc_report_body[0] = 0;
    hevc_csv_text(&csv, "type,synthetic_hevc_proof\nversion,2\noptimization,");
    hevc_csv_text(&csv, FILM_HEVC_OPTIMIZATION_LABEL);
    hevc_csv_text(&csv, "\nphase_timing,encode_includes_synthesis_FIO_CRC_yields\n");
    hevc_csv_value(&csv, "requested_size", proof->size_choice);
    hevc_csv_value(&csv, "width", proof->width);
    hevc_csv_value(&csv, "height", proof->height);
    hevc_csv_value(&csv, "frames_requested", proof->frames_requested);
    hevc_csv_value(&csv, "frames_encoded", proof->frames_encoded);
    hevc_csv_value(&csv, "ctus_encoded", proof->ctus_encoded);
    hevc_csv_value(&csv, "proof_state", (uint32_t)proof->state);
    hevc_csv_text(&csv, "proof_state_name,");
    hevc_csv_text(&csv, (unsigned)proof->state < sizeof(state_names)/sizeof(state_names[0]) ?
                        state_names[(unsigned)proof->state] : "invalid");
    hevc_csv_text(&csv, "\n");
    hevc_csv_value(&csv, "codec_called", proof->codec_called);
    hevc_csv_value(&csv, "codec_result", proof->codec_result);
    hevc_csv_value(&csv, "codec_bytes_committed", proof->codec_bytes_committed);
    hevc_csv_value(&csv, "accepted_bytes", proof->accepted_bytes);
    hevc_csv_value(&csv, "written_bytes", proof->written_bytes);
    hevc_csv_value(&csv, "crc32", proof->crc32);
    hevc_csv_value(&csv, "proof_elapsed_ms", proof->elapsed_ms);
    hevc_csv_value(&csv, "budget_ms", proof->budget_ms);
    hevc_csv_value(&csv, "output_limit_bytes", FILM_HEVC_OUTPUT_LIMIT);
    hevc_csv_value(&csv, "buffer_bytes", proof->buffer_bytes);
    hevc_csv_value(&csv, "workspace_bytes", (uint32_t)sizeof(hi_workspace));
    hevc_csv_value(&csv, "write_callback_calls", proof->write_callback_calls);
    hevc_csv_value(&csv, "fio_write_calls", proof->fio_write_calls);
    hevc_csv_value(&csv, "fio_read_calls", proof->fio_read_calls);
    hevc_csv_value(&csv, "fio_read_bytes", proof->fio_read_bytes);
    hevc_csv_value(&csv, "phase_started_mask", proof->phase_started_mask);
    hevc_csv_value(&csv, "phase_completed_mask", proof->phase_completed_mask);
    hevc_csv_value(&csv, "stopped_phase", proof->stopped_phase);
    for (i = 0; i < FILM_HEVC_PHASE_COUNT; ++i) hevc_csv_value(&csv, phase_names[i], proof->phase_ms[i]);
    hevc_csv_value(&csv, "hvc_published", proof->path[0] != 0);
    hevc_csv_value(&csv, "hvc_verified", proof->state == FILM_HEVC_SAVED);
    hevc_csv_text(&csv, "hvc_path,"); hevc_csv_text(&csv, proof->path);
    hevc_csv_text(&csv, "\nattempted_hvc_path,"); hevc_csv_text(&csv, attempt_path);
    hevc_csv_text(&csv, "\n");
    hevc_csv_value(&csv, "report_budget_ms", FILM_HEVC_REPORT_DEADLINE_MS);
    hevc_csv_text(&csv, "camera_capture,0\nrecording_fps_measured,0\n");
    return csv.failed ? 0u : csv.used;
}

static int hevc_report_verify(hevc_report_job *report, const char *path,
                               size_t length, uint8_t *buffer)
{
    FILE *input = NULL;
    uint32_t size;
    size_t done = 0;
    int ok = 0;
    if (hevc_report_interrupted(report) || FIO_GetFileSize(path, &size) || size != length ||
        hevc_report_interrupted(report)) return 0;
    input = FIO_OpenFile(path, O_RDONLY);
    if (!input) return 0;
    while (done < length) {
        size_t amount = length - done;
        int got;
        if (hevc_report_interrupted(report)) goto done;
        if (amount > FILM_HEVC_REPORT_VERIFY_BYTES) amount = FILM_HEVC_REPORT_VERIFY_BYTES;
        got = FIO_ReadFile(input, buffer, amount);
        if (got <= 0 || (size_t)got > amount ||
            memcmp(buffer, hevc_report_body + done, (size_t)got)) goto done;
        done += (size_t)got;
    }
    if (hevc_report_interrupted(report)) goto done;
    ok = !FIO_GetFileSize(path, &size) && size == length;
    if (hevc_report_interrupted(report)) ok = 0;
done:
    FIO_CloseFile(input);
    return ok;
}

/* Called only after all proof allocations/handles are released. The CSV holds
 * an immutable proof snapshot; report failures touch only report_* fields. */
static void hevc_save_report(const struct film_hevc_results *proof, const char *attempt_path)
{
    hevc_report_job report;
    uint8_t *buffer = NULL;
    FILE *output = NULL;
    char temporary[32] = "", destination[32] = "";
    size_t length;
    int slot, created = 0, exists, wrote;
    report.started = (uint32_t)get_ms_clock(); report.io_started = 0;
    hevc_results.report_state = FILM_HEVC_REPORT_RUNNING;
    if (proof->state == FILM_HEVC_CANCELLED || proof->state == FILM_HEVC_BUSY) {
        hevc_results.report_state = FILM_HEVC_REPORT_SKIPPED_GUARD;
        goto done;
    }
    if (hevc_report_interrupted(&report)) goto done;
    length = hevc_make_report(proof, attempt_path);
    if (!length) goto io_error;
    buffer = fio_malloc(FILM_HEVC_REPORT_VERIFY_BYTES);
    if (!buffer) { hevc_results.report_state = FILM_HEVC_REPORT_NO_MEMORY; goto done; }
    /* Classification arithmetic only; never dereference a transformed alias. */
    if (CACHEABLE(buffer) == buffer) {
        hevc_results.report_state = FILM_HEVC_REPORT_MEMORY_CLASS_ERROR; goto done;
    }
    for (slot = 1; slot <= HEVC_SLOTS; ++slot) {
        if (hevc_report_interrupted(&report)) goto done;
        snprintf(destination, sizeof(destination), "ML/FILM/HVCR%04d.CSV", slot);
        snprintf(temporary, sizeof(temporary), "ML/FILM/HVCR%04d.TMP", slot);
        if (strlen(destination) != 20u || strlen(temporary) != 20u) goto io_error;
        report.io_started = 1;
        exists = is_file(destination);
        if (hevc_report_interrupted(&report)) goto done;
        exists = is_file(temporary) || exists;
        if (!exists) break;
    }
    if (slot > HEVC_SLOTS) { hevc_results.report_state = FILM_HEVC_REPORT_SLOTS_FULL; goto done; }
    if (hevc_report_interrupted(&report)) goto done;
    exists = is_file(destination);
    if (hevc_report_interrupted(&report)) goto done;
    if (is_file(temporary) || exists) goto io_error;
    if (hevc_report_interrupted(&report)) goto done;
    output = FIO_OpenFile(temporary, HEVC_T6_CREATE_NEW);
    if (!output) goto io_error;
    created = 1;
    if (hevc_report_interrupted(&report)) goto done;
    wrote = FIO_WriteFile(output, hevc_report_body, length);
    if (wrote > 0 && (size_t)wrote <= length) hevc_results.report_bytes = (uint32_t)wrote;
    if (wrote < 0 || (size_t)wrote != length) goto io_error;
    FIO_CloseFile(output); output = NULL;
    if (hevc_report_interrupted(&report)) goto done;
    if (!hevc_report_verify(&report, temporary, length, buffer)) goto verify_error;
    if (hevc_report_interrupted(&report)) goto done;
    exists = is_file(destination);
    if (hevc_report_interrupted(&report)) goto done;
    if (exists || FIO_RenameFile(temporary, destination)) goto io_error;
    created = 0;
    memcpy(hevc_results.report_path, destination, strlen(destination) + 1u);
    if (!hevc_report_verify(&report, destination, length, buffer)) goto verify_error;
    if (hevc_report_interrupted(&report)) goto done;
    hevc_results.report_state = FILM_HEVC_REPORT_SAVED;
    goto done;
verify_error:
    if (hevc_results.report_state == FILM_HEVC_REPORT_RUNNING)
        hevc_results.report_state = FILM_HEVC_REPORT_VERIFY_ERROR;
    goto done;
io_error:
    hevc_results.report_state = FILM_HEVC_REPORT_IO_ERROR;
done:
    if (output) FIO_CloseFile(output);
    if (created) FIO_RemoveFile(temporary);
    if (buffer) fio_free(buffer);
    hevc_results.report_elapsed_ms = (uint32_t)((uint32_t)get_ms_clock() - report.started);
}

int film_hevc_generate(unsigned size_choice)
{
    static const uint32_t widths[3] = {320u, 3840u, 7680u};
    static const uint32_t heights[3] = {180u, 2160u, 4320u};
    static const uint32_t budgets[3] = {FILM_HEVC_DEADLINE_MS,
        FILM_HEVC_4K_DEADLINE_MS, FILM_HEVC_8K_DEADLINE_MS};
    struct film_hevc_results proof;
    hevc_job job;
    hi_config config;
    hi_io io;
    hi_stats stats;
    hi_workspace *workspace = NULL;
    char temporary[32] = "", destination[32] = "", status[48];
    int slot = 0, created = 0, result = -1;
    hi_result encoded = HI_BAD_ARGUMENT;
    memset(&job, 0, sizeof(job));
    memset(&stats, 0, sizeof(stats));
    memset(&hevc_results, 0, sizeof(hevc_results));
    job.started = (uint32_t)get_ms_clock();
    job.budget = FILM_HEVC_DEADLINE_MS;
    job.crc = 0xffffffffu;
    hevc_results.version = 2;
    hevc_results.state = FILM_HEVC_RUNNING;
    hevc_results.codec_result = FILM_HEVC_CODEC_NOT_CALLED;
    hevc_results.frames_requested = FILM_HEVC_FRAMES;
    hevc_results.size_choice = size_choice;
    hevc_results.buffer_bytes = FILM_HEVC_DMA_BYTES;
    hevc_results.optimization_profile = FILM_HEVC_OPTIMIZATION_PROFILE;
    hevc_phase_begin(&job, FILM_HEVC_PHASE_SETUP);
    if (size_choice >= 3u) { hevc_results.state = FILM_HEVC_BAD_SIZE; goto done; }
    hevc_results.width = widths[size_choice];
    hevc_results.height = heights[size_choice];
    job.budget = budgets[size_choice];
    hevc_results.budget_ms = job.budget;
    if (hevc_interrupted(&job)) goto done;
    workspace = malloc(sizeof(*workspace));
    if (!workspace) { hevc_results.state = FILM_HEVC_NO_MEMORY; goto done; }
    if (hevc_interrupted(&job)) goto done;
    job.dma = fio_malloc(FILM_HEVC_DMA_BYTES);
    if (!job.dma) { hevc_results.state = FILM_HEVC_NO_MEMORY; goto done; }
    if (hevc_interrupted(&job)) goto done;
    if (CACHEABLE(job.dma) == job.dma) {
        hevc_results.state = FILM_HEVC_MEMORY_CLASS_ERROR; goto done;
    }
    for (slot = 1; slot <= HEVC_SLOTS; ++slot) {
        if (hevc_interrupted(&job)) goto done;
        snprintf(destination, sizeof(destination), "ML/FILM/HVCT%04d.HVC", slot);
        snprintf(temporary, sizeof(temporary), "ML/FILM/HVCT%04d.TMP", slot);
        if (strlen(destination) != 20u || strlen(temporary) != 20u) goto io_error;
        if (!is_file(destination) && !is_file(temporary)) break;
    }
    if (slot > HEVC_SLOTS) goto io_error;
    if (hevc_interrupted(&job)) goto done;
    if (is_file(destination) || is_file(temporary)) goto io_error;
    if (hevc_interrupted(&job)) goto done;
    job.output = FIO_OpenFile(temporary, HEVC_T6_CREATE_NEW);
    if (!job.output) goto io_error;
    created = 1;
    hevc_phase_end(&job, 1);
    config.width = hevc_results.width;
    config.height = hevc_results.height;
    config.frames = FILM_HEVC_FRAMES;
    config.output_limit = FILM_HEVC_OUTPUT_LIMIT;
    io.context = &job;
    io.write = hevc_write;
    io.read_ctu = hevc_read_ctu;
    io.cancelled = hevc_cancelled;
    film_t6_set_status("HEVC proof: SYNTHETIC ONLY");
    hevc_phase_begin(&job, FILM_HEVC_PHASE_ENCODE);
    hevc_results.codec_called = 1;
    encoded = hi_encode(&config, &io, workspace, &stats);
    hevc_results.codec_result = (uint32_t)encoded;
    if (encoded != HI_OK || stats.frames_encoded != FILM_HEVC_FRAMES ||
        stats.bytes_committed != job.accepted || !job.accepted) {
        if (hevc_results.state == FILM_HEVC_RUNNING) hevc_results.state = FILM_HEVC_CODEC_ERROR;
        goto done;
    }
    /* Only a successful codec may flush its pending tail. Cleanup NEVER
     * flushes failed/canceled buffers, even when two frames were accepted. */
    if (!hevc_flush(&job)) goto done;
    FIO_CloseFile(job.output); job.output = NULL;
    free(workspace); workspace = NULL;
    if (hevc_interrupted(&job)) goto done;
    if (job.written != job.accepted || job.pending) {
        hevc_results.state = FILM_HEVC_CODEC_ERROR; goto done;
    }
    hevc_phase_end(&job, 1);
    hevc_phase_begin(&job, FILM_HEVC_PHASE_TMP_VERIFY);
    if (!hevc_verify(&job, temporary, job.dma)) goto verify_error;
    if (hevc_interrupted(&job)) goto done;
    hevc_phase_end(&job, 1);
    hevc_phase_begin(&job, FILM_HEVC_PHASE_RENAME);
    if (hevc_interrupted(&job)) goto done;
    {
        int exists = is_file(destination);
        if (hevc_interrupted(&job)) goto done;
        if (exists || FIO_RenameFile(temporary, destination)) goto io_error;
    }
    created = 0; /* Publication boundary: NEVER delete the destination. */
    memcpy(hevc_results.path, destination, strlen(destination) + 1u);
    hevc_phase_end(&job, 1);
    hevc_phase_begin(&job, FILM_HEVC_PHASE_FINAL_VERIFY);
    if (!hevc_verify(&job, destination, job.dma)) goto verify_error;
    if (hevc_interrupted(&job)) goto done;
    hevc_phase_end(&job, 1);
    hevc_results.state = FILM_HEVC_SAVED;
    result = 0;
    goto done;
verify_error:
    if (hevc_results.state == FILM_HEVC_RUNNING) hevc_results.state = FILM_HEVC_VERIFY_ERROR;
    goto done;
io_error:
    hevc_results.state = FILM_HEVC_IO_ERROR;
done:
    hevc_phase_end(&job, 0);
    if (job.output) FIO_CloseFile(job.output);
    if (workspace) free(workspace);
    if (job.dma) fio_free(job.dma);
    if (created) FIO_RemoveFile(temporary);
    hevc_results.bytes = job.written;
    hevc_results.crc32 = job.crc ^ 0xffffffffu;
    hevc_results.accepted_bytes = job.accepted;
    hevc_results.written_bytes = job.written;
    hevc_results.codec_bytes_committed = stats.bytes_committed <= UINT32_MAX ?
        (uint32_t)stats.bytes_committed : UINT32_MAX;
    hevc_results.frames_encoded = stats.frames_encoded;
    hevc_results.ctus_encoded = stats.ctus_encoded;
    hevc_results.elapsed_ms = (uint32_t)((uint32_t)get_ms_clock() - job.started);
    /* Freeze all primary fields before diagnostics. Export has a fresh budget
     * and can neither turn an HVC failure into success nor change its reason. */
    proof = hevc_results;
    hevc_save_report(&proof, destination);
    if (hevc_results.state == FILM_HEVC_CANCELLED) {
        film_t6_set_status("HEVC proof canceled"); result = 1;
    } else if (hevc_results.state == FILM_HEVC_BUSY) {
        film_t6_set_status("HEVC proof stopped for camera"); result = 1;
    } else if (hevc_results.state == FILM_HEVC_DEADLINE) {
        if (hevc_results.report_state == FILM_HEVC_REPORT_SAVED) {
            snprintf(status, sizeof(status), "HEVC limit; %s", hevc_results.report_path + 8);
            film_t6_set_status(status);
        } else film_t6_set_status("HEVC proof reached time limit");
        result = 1;
    } else if (!result) {
        snprintf(status, sizeof(status), "HEVC synthetic: HVCT%04d.HVC", slot);
        film_t6_set_status(status);
    } else if (result) {
        film_t6_set_status(hevc_results.path[0] ? "HEVC proof kept; verify failed" :
                           "HEVC proof not saved/verified");
    }
    if (result == 1 && hevc_results.path[0])
        film_t6_set_status("HEVC proof kept; verify stopped");
    return result;
}
