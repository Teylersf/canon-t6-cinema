/* T6 Cinema offscreen synthetic CPU test. GPL-2.0-only. */
#ifndef FILM_CPU_BENCH_H
#define FILM_CPU_BENCH_H

#include <stdint.h>

#define FILM_CPU_CASES 3
#define FILM_CPU_RUNS 3

enum film_cpu_state {
    FILM_CPU_IDLE = 0,
    FILM_CPU_RUNNING,
    FILM_CPU_SAVED,
    FILM_CPU_CANCELLED,
    FILM_CPU_BUSY,
    FILM_CPU_DEADLINE,
    FILM_CPU_NO_MEMORY,
    FILM_CPU_KERNEL_ERROR,
    FILM_CPU_REPORT_ERROR
};

struct film_cpu_case {
    uint32_t width, height;
    uint32_t milliseconds[FILM_CPU_RUNS];
    uint32_t count, minimum_ms, maximum_ms;
    uint32_t warmup_done;
};

struct film_cpu_results {
    uint32_t version;
    enum film_cpu_state state;
    uint32_t elapsed_ms, cases_completed;
    struct film_cpu_case cases[FILM_CPU_CASES];
    char report_path[32];
};

/* Existing worker serializes this call, marks processing, disables Auto and
 * queued Develop. The queue action clears stale cancellation; any later Stop
 * must survive until this function checks it. Synthetic owned pixels only;
 * no camera frame buffers, capture/encoder hooks or sensor/timer writes.
 * 0 = complete or deadline-partial CSV saved; 1 = cancel/busy; -1 = error.
 * The 10s deadline is cooperative BETWEEN frames, never a hard time limit. */
int film_cpu_bench(void);

/* Fixed RAM results; read after the worker completes. Not an atomic snapshot.
 * DEADLINE with a nonempty report_path means partial measurements were saved;
 * count identifies valid samples, and CSV represents missing values as -1.
 * Timings cover only se_ya_apply on synthetic generic YUYV/studio/unsigned128.
 * They do not measure encoder throughput, recording FPS or unique exposures. */
const struct film_cpu_results *film_cpu_bench_get_results(void);

#endif
