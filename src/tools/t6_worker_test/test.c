/* Regression harness for the ACTUAL camera worker, not a reimplementation.
 * DryOS scheduling and file I/O are mocked in memory. No camera/card access.
 * This does not establish physical camera safety or Canon filesystem behavior.
 * GPL v2. */
#include "dryos.h"
#include "fio-ml.h"
#include "lens.h"
#include "film_t6.h"
#include "jpeg_dec.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define SOURCE "A:/DCIM/100CANON/IMG_0001.CR2"
#define DEST "A:/DCIM/100CANON/T6F_0001.JPG"
#define TEMP "A:/DCIM/100CANON/T6F_0001.TMP"
typedef struct { char path[128]; uint8_t *data; size_t len, cap; int exists, owned; } entry;
typedef struct { entry *file; size_t offset; int writing, open; } handle;
static entry files[8];
static handle handles[8];
static uint8_t *fixture, *recipe;
static size_t fixture_len, recipe_len;
static unsigned clock_ms;
static int cancel_now, auto_on, writes, removes, renames, creates, source_ops;
static int inject_cancel, inject_busy, inject_auto_off, inject_write_failure;
static int inject_source_growth, inject_rename_failure, inject_tmp_race;
static int short_reads, read_calls, inject_read_failure, inject_deadline, inject_shutdown;
static int inject_open_race, reject_exclusive, exclusive_opens, legacy_creates;
static char status[80];
int ml_started = 1, ml_shutdown_requested;
struct mock_lens_info lens_info;

static entry *find(const char *path)
{
    int i;
    for (i = 0; i < 8; i++) if (files[i].exists && !strcmp(files[i].path, path)) return &files[i];
    return NULL;
}
static entry *make_entry(const char *path, const void *data, size_t size, int copy)
{
    int i;
    entry *e = find(path);
    if (!e) for (i = 0; i < 8; i++) if (!files[i].exists) { e = &files[i]; break; }
    CHECK(e);
    if (e->owned) free(e->data);
    memset(e, 0, sizeof(*e));
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->exists = 1; e->len = e->cap = size; e->owned = copy;
    e->data = copy ? (uint8_t *)malloc(size ? size : 1) : (uint8_t *)data;
    CHECK(!copy || e->data);
    if (copy && size) memcpy(e->data, data, size);
    return e;
}
static handle *new_handle(entry *e, int writing)
{
    int i;
    for (i = 0; i < 8; i++) if (!handles[i].open) {
        handles[i].file = e; handles[i].offset = 0;
        handles[i].writing = writing; handles[i].open = 1; return &handles[i];
    }
    CHECK(0); return NULL;
}
static void reset(void)
{
    int i;
    for (i = 0; i < 8; i++) if (files[i].owned) free(files[i].data);
    memset(files, 0, sizeof(files)); memset(handles, 0, sizeof(handles));
    make_entry(SOURCE, fixture, fixture_len, 0);
    make_entry("ML/FILM/portra.fs", recipe, recipe_len, 0);
    clock_ms = 0; cancel_now = 0; auto_on = 1; lens_info.job_state = 0;
    writes = removes = renames = creates = source_ops = 0;
    inject_cancel = inject_busy = inject_auto_off = inject_write_failure = 0;
    inject_source_growth = inject_rename_failure = inject_tmp_race = short_reads = 0;
    read_calls = inject_read_failure = inject_deadline = inject_shutdown = 0;
    inject_open_race = reject_exclusive = exclusive_opens = legacy_creates = 0;
    ml_shutdown_requested = 0; status[0] = 0;
}
unsigned get_ms_clock(void) { return clock_ms; }
void msleep(int ms)
{
    clock_ms += (unsigned)ms;
    if (inject_tmp_race && clock_ms >= 600 && !find(TEMP))
        make_entry(TEMP, "KEEP", 4, 1);
}
void *fio_malloc(size_t size) { return malloc(size); }
void fio_free(void *ptr) { free(ptr); }
int get_halfshutter_pressed(void) { return 0; }
int is_movie_mode(void) { return 0; }
struct card_info *get_shooting_card(void) { static struct card_info c = {1}; return &c; }
const char *get_dcim_dir(void) { return "A:/DCIM/100CANON"; }
const char *get_file_prefix(void) { return "IMG_"; }
int is_file(const char *path) { return find(path) != NULL; }
FILE *FIO_OpenFile(const char *path, unsigned mode)
{
    entry *e = find(path);
    if (!strcmp(path, SOURCE) && mode != O_RDONLY) source_ops++;
    if (mode == O_RDONLY) return e ? (FILE *)new_handle(e, 0) : NULL;
    /* Model an API that honors O_EXCL. Rejection models a body that does not
     * support this combination; the worker must fail without a fallback. */
    if (mode != (O_WRONLY | O_CREAT | O_EXCL) || strcmp(path, TEMP)) return NULL;
    exclusive_opens++;
    /* The competing file appears AFTER the worker's last existence check,
     * inside the actual open. An existence precheck cannot protect it. */
    if (inject_open_race && !e) e = make_entry(TEMP, "KEEP", 4, 1);
    if (e || reject_exclusive) return NULL;
    creates++;
    return (FILE *)new_handle(make_entry(path, NULL, 0, 1), 1);
}
FILE *FIO_CreateFile(const char *path)
{
    if (!strcmp(path, SOURCE)) source_ops++;
    /* Faithfully destructive: upstream ML explicitly removes an existing
     * path before creating it. Never pretend this is an exclusive open. */
    legacy_creates++;
    creates++;
    return (FILE *)new_handle(make_entry(path, NULL, 0, 1), 1);
}
int FIO_ReadFile(FILE *file, void *dst, size_t count)
{
    handle *h = (handle *)file;
    CHECK(h->open && !h->writing);
    if (inject_read_failure && ++read_calls > 20) return -1;
    if (h->offset > h->file->len) return -1;
    if (count > h->file->len - h->offset) count = h->file->len - h->offset;
    if (short_reads && count > 97) count = 97;
    memcpy(dst, h->file->data + h->offset, count); h->offset += count;
    return (int)count;
}
int FIO_WriteFile(FILE *file, const void *src, size_t count)
{
    handle *h = (handle *)file;
    size_t n = inject_write_failure && writes ? count / 2 : count;
    CHECK(h->open && h->writing && !strcmp(h->file->path, TEMP));
    if (h->offset + n > h->file->cap) {
        h->file->cap = h->offset + n + 4096;
        h->file->data = (uint8_t *)realloc(h->file->data, h->file->cap);
        CHECK(h->file->data);
    }
    memcpy(h->file->data + h->offset, src, n); h->offset += n;
    h->file->len = h->offset; writes++;
    if (writes >= 2) {
        if (inject_cancel) cancel_now = 1;
        if (inject_busy) lens_info.job_state = 1;
        if (inject_auto_off) auto_on = 0;
        if (inject_deadline) clock_ms += 400000u;
        if (inject_shutdown) ml_shutdown_requested = 1;
    }
    return (int)n;
}
void FIO_CloseFile(FILE *file) { handle *h = (handle *)file; CHECK(h->open); h->open = 0; }
int FIO_GetFileSize(const char *path, uint32_t *size)
{
    entry *e = find(path);
    if (!e) return -1;
    *size = (uint32_t)e->len;
    if (!strcmp(path, SOURCE) &&
        ((inject_source_growth == 1 && clock_ms >= 600) ||
         (inject_source_growth == 2 && writes >= 2))) (*size)++;
    return 0;
}
int64_t FIO_SeekSkipFile(FILE *file, int64_t offset, int whence)
{
    handle *h = (handle *)file;
    CHECK(h->open && whence == SEEK_SET);
    if (offset < 0 || (uint64_t)offset > h->file->len) return -1;
    h->offset = (size_t)offset; return offset;
}
int FIO_RenameFile(const char *src, const char *dst)
{
    entry *e = find(src);
    if (!strcmp(src, SOURCE) || !strcmp(dst, SOURCE)) source_ops++;
    CHECK(!strcmp(src, TEMP) && !strcmp(dst, DEST) && !find(dst));
    if (inject_rename_failure) return -1;
    CHECK(e); snprintf(e->path, sizeof(e->path), "%s", dst); renames++; return 0;
}
int FIO_RemoveFile(const char *path)
{
    entry *e = find(path);
    if (!strcmp(path, SOURCE)) source_ops++;
    CHECK(!strcmp(path, TEMP));
    if (!e) return -1;
    e->exists = 0; removes++; return 0;
}
size_t read_file(const char *path, void *dst, size_t size)
{
    entry *e = find(path);
    if (!e) return (size_t)-1;
    if (size > e->len) size = e->len;
    memcpy(dst, e->data, size); return size;
}
int film_t6_auto_enabled(void) { return auto_on; }
int film_t6_take_request(void) { return 0; }
int film_t6_cancelled(void) { return cancel_now || ml_shutdown_requested; }
void film_t6_clear_cancel(void) { cancel_now = 0; }
void film_t6_snapshot(struct film_t6_settings *s) { snprintf(s->stock, sizeof(s->stock), "portra.fs"); s->effect = 2; s->amber = 0; }
void film_t6_load_stocks(void) {}
void film_t6_set_status(const char *s) { snprintf(status, sizeof(status), "%s", s); }

#include "../../../integrations/magiclantern/1300D.110/src/film_develop.c"

static uint8_t *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *data;
    CHECK(f && !fseek(f, 0, SEEK_END)); n = ftell(f); CHECK(n > 0);
    CHECK(!fseek(f, 0, SEEK_SET)); data = (uint8_t *)malloc((size_t)n); CHECK(data);
    CHECK(fread(data, 1, (size_t)n, f) == (size_t)n); fclose(f); *size = (size_t)n; return data;
}
static int develop(int automatic)
{
    struct film_t6_settings settings;
    int result, i;
    film_t6_snapshot(&settings);
    result = film_develop(SOURCE, DEST, TEMP, &settings, automatic);
    CHECK(!source_ops && !legacy_creates && find(SOURCE)->data == fixture && find(SOURCE)->len == fixture_len);
    for (i = 0; i < 8; i++) CHECK(!handles[i].open);
    return result;
}
static void failed_cleanly(void)
{
    CHECK(develop(0) == -1);
    CHECK(!find(DEST) && !find(TEMP) && !renames);
}
int main(int argc, char **argv)
{
    entry *e;
    se_jdec_info info;
    CHECK(argc == 3); fixture = load(argv[1], &fixture_len); recipe = load(argv[2], &recipe_len);
    reset(); short_reads = 1;
    CHECK(develop(0) == 0);
    e = find(DEST); CHECK(e && !find(TEMP) && creates == 1 && renames == 1 && !removes);
    CHECK(se_jpeg_dec_info(e->data, e->len, &info) == SE_OK);
    CHECK(info.width == 2592 && info.height == 1728);
    CHECK(!strcmp(status, "JPEG saved; CR2 untouched"));
    { const char *path = getenv("T6_WORKER_OUTPUT");
      if (path && path[0]) {
          FILE *f = fopen(path, "wb"); CHECK(f);
          CHECK(fwrite(e->data, 1, e->len, f) == e->len); CHECK(!fclose(f));
      } }
    CHECK(develop(0) == 1 && creates == 1 && renames == 1); /* repeated manual */
    reset(); make_entry(DEST, "KEEP", 4, 1);
    CHECK(develop(0) == 1 && !memcmp(find(DEST)->data, "KEEP", 4) && !creates);
    reset(); make_entry(TEMP, "KEEP", 4, 1);
    CHECK(develop(0) == 1 && !memcmp(find(TEMP)->data, "KEEP", 4) && !removes);
    reset(); inject_tmp_race = 1;
    CHECK(develop(0) == -1 && !creates && !removes && !memcmp(find(TEMP)->data, "KEEP", 4));
    reset(); inject_open_race = 1;
    CHECK(develop(0) == -1 && exclusive_opens == 1 && !creates && !removes &&
          !find(DEST) && find(TEMP)->len == 4 && !memcmp(find(TEMP)->data, "KEEP", 4));
    reset(); reject_exclusive = 1;
    CHECK(develop(0) == -1 && exclusive_opens == 1 && !creates && !removes &&
          !find(TEMP) && !find(DEST));
    reset(); cancel_now = 1; failed_cleanly(); CHECK(!creates);
    reset(); inject_cancel = 1; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_busy = 1; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_write_failure = 1; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_rename_failure = 1; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_source_growth = 1; failed_cleanly(); CHECK(!creates);
    reset(); inject_source_growth = 2; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_read_failure = 1; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_deadline = 1; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_shutdown = 1; failed_cleanly(); CHECK(creates == 1 && removes == 1);
    reset(); inject_auto_off = 1;
    CHECK(develop(1) == -1 && !find(DEST) && !find(TEMP) && creates == 1 && removes == 1);
    reset(); find(SOURCE)->len = 8; /* deliberately truncated header */
    { struct film_t6_settings settings; film_t6_snapshot(&settings);
      CHECK(film_develop(SOURCE, DEST, TEMP, &settings, 0) == -1 && !creates); }
    reset();
    { int i; for (i = 0; i < 8; i++) if (files[i].owned) free(files[i].data); }
    free(fixture); free(recipe);
    puts("t6 worker: real CR2, short reads, originals, exclusive-open races/rejection, collisions, cancel, capture, writes, rename, growth, truncation passed");
    return 0;
}
