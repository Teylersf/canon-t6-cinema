# Experimental fixed-field video observer

This portable C99 prototype records word values through a caller-supplied
callback. It has no native address map, camera bridge, hooks, MMIO access,
pixel access, file I/O, UI, recording requests or r12 integration. It starts
inactive. No hardware observation or camera timing result is implied.

The fixed symbolic profile contains exactly seven fields: H.264 input value,
stream1 value, read-handle value, write-handle value, MVR context pointer value,
producer active mask, and producer YUV tracked-address value. Static private
ROM analysis identified corresponding ordinary low-RAM globals; an eventual
separately reviewed private bridge must enforce that exact whitelist. This
folder deliberately contains no ROM addresses or arbitrary-address API. The
core never follows a returned pointer value. The callback contract cannot
prove that an implementation reads the correct address or avoids hardware.

## State and timing

`se_tvp_init` validates caller-owned, aligned record storage and leaves the
probe OFF without changing record bytes. Capacity is 1..512; each record is
72 bytes on the supported host and ARM toolchains, so the maximum record
budget is 36 KiB. The small state object holds no inline array or allocation.
Only `records[0..state.count)` belongs to the current run.

An explicit `se_tvp_arm` starts a 120-second wait. IDLE and PREPARING perform
no reads. ACTIVE requires caller-reported allowed movie Live View, then
starts a five-second observation window. Each due tick reads the seven fields
in fixed order twice, with a clock sample before and after the paired reads.
Records contain both sets of values, those actual snapshot timestamps, a
changed-field mask, failed-read mask and status flags. Failed words are zero
and marked; their equality is not evaluated. Any read failure ends the run
after saving that partial snapshot.

Sampling starts are requested at least five milliseconds apart. A late tick
takes at most one snapshot and schedules no catch-up. The five-second window
and 512-record capacity are independent limits; capacity can finish a densely
sampled run before five seconds. No tick begins reads at or beyond a deadline.
A callback already running cannot be preempted: a snapshot may finish after
the window, is flagged accordingly, and stops further reads. This is a
cooperative bound, not a hard real-time deadline or measured sampling rate.

Cancel, shutdown, capture and disallowed-mode inputs stop before callbacks.
After observation starts, recording end or loss of movie/Live View also stops
without another snapshot, even before the next sampling interval is due.
There is no automatic restart. `se_tvp_reset` returns to OFF and clears the
record count without erasing storage; `se_tvp_arm` can explicitly reuse a
completed run. A DONE/OFF tick is inert, including with null inputs and I/O.

All clocks use the same modulo-2^32 millisecond domain. Unsigned subtraction
handles wrap provided every compared interval is less than 2^31 ms. Backwards
or stale clock readings stop the run. Exact unknown elapsed time after a gap
of at least 2^31 ms cannot be recovered from a 32-bit clock. Caller inputs are
snapshots, not verified hardware guards; mode/cancel changes during the
fourteen callbacks are not observed until another tick. Calls and callbacks
must be serialized, bounded and non-reentrant, with valid stable metadata and
exclusive ownership of the state/record storage. No IRQ or cache operation is
performed by the core.

## Usage

The caller supplies storage, a whitelist-enforcing `read_word` callback and
a same-domain `clock_ms` callback. It retains the records after completion and
decides whether any later export is appropriate. No file exporter is included.

```c
se_tvp_state probe = {0};
se_tvp_record records[64]; /* caller's choice; no mandatory core allocation */
se_tvp_io io = {private_context, whitelisted_read_word, monotonic_clock_ms};

if (se_tvp_init(&probe, records, sizeof(records), 64) == SE_TVP_OK)
    se_tvp_arm(&probe, current_ms);

/* Called later with a fresh caller snapshot; this never requests recording. */
se_tvp_inputs in = {
    current_ms, SE_TVP_RECORDING_ACTIVE,
    SE_TVP_MODE_ALLOWED | SE_TVP_MODE_MOVIE | SE_TVP_MODE_LIVEVIEW,
    0, 0, 0
};
se_tvp_tick(&probe, &in, &io);
```

The paired values are not atomic. Unchanged words cannot prove producer
readiness, encoder ownership/release, DMA lifetime, pixel format, row stride,
cache coherency, an available processing interval, unique exposures or FPS.
No image-processing insertion point is established. The acquired physical CPU
results and unchanged baseline MOVs support further research; the current
uncached kernel timings motivate a controlled memory/compiler comparison
before integrating any native observer or image processing.

## Validation

Run from this folder with GCC and the GNU ARM embedded toolchain installed:

```sh
make test
make asan
make arm
```

The Makefile uses temporary output directories outside the source tree. The
host suite exercises state transitions, clock/deadline boundaries and wrap,
late ticks, storage validation/preservation, cancellation/mode guards, record
limits, paired changes/failures, and an adversarial fixed-ID whitelist. The
sanitizer target enables AddressSanitizer/UndefinedBehaviorSanitizer with
non-recovering failures. The ARM target compiles freestanding for ARM946E-S
and reports object size and undefined symbols; compilation is not execution
or a camera performance measurement.

Equivalent standalone ARM compile command:

```sh
arm-none-eabi-gcc -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
  -mcpu=arm946e-s -marm -ffreestanding -fno-builtin \
  -c t6_video_probe.c -o /tmp/t6_video_probe.o
arm-none-eabi-size -A /tmp/t6_video_probe.o
arm-none-eabi-nm -u /tmp/t6_video_probe.o
```

Source is GPL-2.0-only, consistent with the surrounding project. The state
machine is new observation code; it does not implement an optical anamorphic
effect or change Canon's recording pipeline.
