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
#define HEVC_VERIFY_BYTES 4096u
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
    uint32_t started, written, crc, source_ctus;
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
    if ((uint32_t)((uint32_t)get_ms_clock() - job->started) >= FILM_HEVC_DEADLINE_MS) {
        hevc_results.state = FILM_HEVC_DEADLINE;
        return 1;
    }
    return 0;
}

static int hevc_cancelled(void *context) { return hevc_interrupted(context); }

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

static int hevc_write(void *context, const uint8_t *data, size_t bytes)
{
    hevc_job *job = context;
    int wrote;
    if (hevc_interrupted(job)) return 0;
    if (bytes > FILM_HEVC_OUTPUT_LIMIT - job->written) {
        hevc_results.state = FILM_HEVC_CODEC_ERROR;
        return 0;
    }
    wrote = FIO_WriteFile(job->output, data, bytes);
    if (wrote > 0 && (size_t)wrote <= bytes) job->written += (uint32_t)wrote;
    if (wrote < 0 || (size_t)wrote != bytes) {
        hevc_results.state = FILM_HEVC_IO_ERROR;
        return 0;
    }
    job->crc = hevc_crc(job->crc, data, bytes);
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
        if (amount > HEVC_VERIFY_BYTES) amount = HEVC_VERIFY_BYTES;
        got = FIO_ReadFile(input, buffer, amount);
        if (got <= 0 || (uint32_t)got > amount) goto done;
        crc = hevc_crc(crc, buffer, (size_t)got);
        done += (uint32_t)got;
    }
    if (hevc_interrupted(job)) goto done;
    ok = !FIO_GetFileSize(path, &size) && size == job->written && crc == job->crc;
done:
    FIO_CloseFile(input);
    return ok;
}

int film_hevc_generate(unsigned size_choice)
{
    static const uint32_t widths[3] = {320u, 3840u, 7680u};
    static const uint32_t heights[3] = {180u, 2160u, 4320u};
    hevc_job job;
    hi_config config;
    hi_io io;
    hi_stats stats;
    hi_workspace *workspace = NULL;
    uint8_t *verify_buffer = NULL;
    char temporary[32] = "", destination[32] = "", status[48];
    int slot, created = 0, result = -1;
    hi_result encoded = HI_BAD_ARGUMENT;
    memset(&job, 0, sizeof(job));
    memset(&stats, 0, sizeof(stats));
    memset(&hevc_results, 0, sizeof(hevc_results));
    job.started = (uint32_t)get_ms_clock();
    job.crc = 0xffffffffu;
    hevc_results.version = 1;
    hevc_results.state = FILM_HEVC_RUNNING;
    hevc_results.codec_result = FILM_HEVC_CODEC_NOT_CALLED;
    hevc_results.frames_requested = FILM_HEVC_FRAMES;
    if (size_choice >= 3u) { hevc_results.state = FILM_HEVC_BAD_SIZE; goto done; }
    hevc_results.width = widths[size_choice];
    hevc_results.height = heights[size_choice];
    if (hevc_interrupted(&job)) goto done;
    workspace = malloc(sizeof(*workspace));
    if (!workspace) { hevc_results.state = FILM_HEVC_NO_MEMORY; goto done; }
    if (hevc_interrupted(&job)) goto done;
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
    job.output = FIO_OpenFile(temporary, HEVC_T6_CREATE_NEW);
    if (!job.output) goto io_error;
    created = 1;
    config.width = hevc_results.width;
    config.height = hevc_results.height;
    config.frames = FILM_HEVC_FRAMES;
    config.output_limit = FILM_HEVC_OUTPUT_LIMIT;
    io.context = &job;
    io.write = hevc_write;
    io.read_ctu = hevc_read_ctu;
    io.cancelled = hevc_cancelled;
    film_t6_set_status("HEVC proof: SYNTHETIC ONLY");
    encoded = hi_encode(&config, &io, workspace, &stats);
    hevc_results.codec_result = (uint32_t)encoded;
    /* No full frame, and no cached codec workspace during readback. */
    free(workspace);
    workspace = NULL;
    FIO_CloseFile(job.output);
    job.output = NULL;
    if (hevc_interrupted(&job)) goto done;
    if (encoded != HI_OK || stats.frames_encoded != FILM_HEVC_FRAMES ||
        stats.bytes_committed != job.written || !job.written) {
        if (hevc_results.state == FILM_HEVC_RUNNING) hevc_results.state = FILM_HEVC_CODEC_ERROR;
        goto done;
    }
    verify_buffer = fio_malloc(HEVC_VERIFY_BYTES);
    if (!verify_buffer) { hevc_results.state = FILM_HEVC_NO_MEMORY; goto done; }
    if (!hevc_verify(&job, temporary, verify_buffer)) goto verify_error;
    if (hevc_interrupted(&job)) goto done;
    if (is_file(destination) || FIO_RenameFile(temporary, destination)) goto io_error;
    created = 0; /* Publication boundary: NEVER delete the destination. */
    memcpy(hevc_results.path, destination, strlen(destination) + 1u);
    if (!hevc_verify(&job, destination, verify_buffer)) goto verify_error;
    if (hevc_interrupted(&job)) goto done;
    hevc_results.state = FILM_HEVC_SAVED;
    result = 0;
    snprintf(status, sizeof(status), "HEVC synthetic: HVCT%04d.HVC", slot);
    film_t6_set_status(status);
    goto done;
verify_error:
    if (hevc_results.state == FILM_HEVC_RUNNING) hevc_results.state = FILM_HEVC_VERIFY_ERROR;
    goto done;
io_error:
    hevc_results.state = FILM_HEVC_IO_ERROR;
done:
    if (job.output) FIO_CloseFile(job.output);
    if (workspace) free(workspace);
    if (verify_buffer) fio_free(verify_buffer);
    if (created) FIO_RemoveFile(temporary);
    hevc_results.bytes = job.written;
    hevc_results.crc32 = job.crc ^ 0xffffffffu;
    hevc_results.frames_encoded = stats.frames_encoded;
    hevc_results.ctus_encoded = stats.ctus_encoded;
    hevc_results.elapsed_ms = (uint32_t)((uint32_t)get_ms_clock() - job.started);
    if (hevc_results.state == FILM_HEVC_CANCELLED) {
        film_t6_set_status("HEVC proof canceled"); result = 1;
    } else if (hevc_results.state == FILM_HEVC_BUSY) {
        film_t6_set_status("HEVC proof stopped for camera"); result = 1;
    } else if (hevc_results.state == FILM_HEVC_DEADLINE) {
        film_t6_set_status("HEVC proof: cooperative 30s limit"); result = 1;
    } else if (result) {
        film_t6_set_status(hevc_results.path[0] ? "HEVC proof kept; verify failed" :
                           "HEVC proof not saved/verified");
    }
    if (result == 1 && hevc_results.path[0])
        film_t6_set_status("HEVC proof kept; verify stopped");
    return result;
}
