# T6 Cinema — experimental 1300D.110 integration

**EXPERIMENTAL, LIMITED PHYSICAL VALIDATION.** The r11 build booted and produced
two readable JPEGs on the owner's T6; the latest ETERNA/STRONG/BLUE image matched
the desktop codestream exactly. This does not establish sustained reliability.
Target: Canon EOS Rebel T6 / EOS 1300D /
Kiss X80 with Canon firmware **1.1.0 only**. T6i/750D, T5/1200D and firmware
1.2.0 are different targets. No installer or boot-flag writer is supplied.

This adds a **T6 Film** page to Magic Lantern's existing menus. It retains the
pinned upstream 1300D platform, entry points and original limited features.
There are no borrowed 5D/50D ROM addresses, sensor-mode changes, RAW video unlocks,
or live video effects. The optional Retro dashboard uses native custom-menu
drawing and checked source-level button gates in the pinned ML event chain;
it adds no binary GUI-hook patch or new firmware address. It remains hardware
untested. See the repository's `docs/RETRO_UI.md` for controls and verification.
The staged ML menu uses explicit open/close intent so a shutter handler and
pending redraw cannot accidentally close and then reopen the menu.

The worker reads the JPEG preview embedded in a CR2, grades it with a Soup
film stock, applies optional anamorphic-inspired highlight streaks, bloom,
vignette and lateral color fringing, and writes a **separate** half-resolution
JPEG. These effects do not produce optical oval bokeh or an anamorphic field
of view. The JPEG uses baseline 4:2:2 encoding; physical T6 playback compatibility
and latency have not been verified. Transfer the JPEG to a computer to inspect
it; the camera may not list a newly created image until it rebuilds its index.

The source CR2 is opened read-only and is never rewritten, preview-patched,
renamed or deleted. Monochrome Canon Picture Styles have already removed color
from the embedded preview; use a color Picture Style for color stocks.

## Menu

- **Auto new photos:** always OFF at boot. Enabling establishes the current
  file number as a baseline; only subsequent photos are considered. Each poll
  processes at most the newest four numbers. Older burst frames are skipped.
- **Develop latest:** processes the current folder's latest numbered CR2 once,
  even when automatic processing is off. It does not scan a card or select
  arbitrary historical files.
- **Film stock:** reads up to 16 validated names from `ML/FILM/INDEX.TXT`.
  The package uses stable 8.3 filenames for the four longer source filenames;
  recipe names and ordering stay the same. Native T6 emulation could not open
  those long paths but could read their short aliases. Do not substitute the
  desktop index for the packaged index.
- **Cinema effects:** OFF, Subtle, Cinema, Strong. **Streak tint:** Blue or Amber.
- **Cancel / stop auto:** disables automatic processing and cancels at a row or
  I/O boundary. Shutting down also requests cancellation.
- **CPU limits test:** a manual, offscreen YUV workload, serialized with still
  processing. It turns Auto off, clears queued Develop, and waits outside
  capture, half-shutter, LV and movie contexts. It runs generated 320×180,
  640×360 and 640×480 frames with one warmup and three timed calls per case.
  No camera image buffers, encoder hooks or sensor timers are accessed.

Successful photo status includes elapsed seconds and milliseconds for the whole
job, including stabilization, processing, cleanup and I/O. This is wall time,
not an isolated CPU-cycle count.

The CPU test uses one 615,680-byte allocation for pixels and row scratch, freed
before report I/O. Its 10-second budget is cooperative between frames, not a
hard execution limit. Cancel, capture or incompatible mode stops suppress the
report. An idle deadline may save partial samples with explicit counts and -1
for missing values. New reports use `ML/FILM/CPUB0001.CSV` through slot 0016,
an exclusively created temporary, byte readback, and guarded rename; existing
reports and other files are preserved. The CSV marks these as synthetic kernel
timings. They establish no recording FPS, encoder format or DMA/cache ownership.

Output beside `IMG_1234.CR2`: `T6F_1234.JPG`, via a new `T6F_1234.TMP`.
The worker refuses detected existing output or temporary files. It requests
temporary creation with T6-native flags **0x8501**: write 0x1, create 0x100,
exclusive 0x400 and check-existing 0x8000. Truncate 0x200 is absent. The global ML
`O_CREAT`/`O_EXCL` macros remain unchanged; their combined 0xC1 value is unsuitable
for this native T6 operation. A rejected open aborts the job; there is no fallback
to `FIO_CreateFile`, whose upstream implementation removes existing paths. Only
a temporary file opened by the current job can be cleaned up. A successful job
checks encoded length and JPEG start/end markers before publishing its output.
The destination is rechecked before native `FIO_RenameFile`.

**Actual-firmware emulator tests** completed a valid 1728-row JPEG, rejected a
TMP created after the final existence check with EEXIST (17), and rejected a
destination created immediately before native rename with return -1. Both
collisions preserved all eight pre-existing bytes. See the repository's
`docs/NATIVE_FILE_IO.md`. These native paths are emulator-tested and
**physical-camera untested**; publication is not claimed to be universally
atomic, nor do emulator results establish real-card behavior or responsiveness.
Failed frames remain unchanged and may be retried with Develop latest.
Source file size must be stable for 600 ms. The worker waits while Canon reports
a capture job, the shutter is half-pressed, or movie mode is selected; a new
capture or movie-mode switch aborts processing at the next row/read boundary.
The aborted frame needs a manual retry. Automatic operation is experimental
and may still contend with shooting. Use single-shot RAW initially.

Bounds: 64 MiB CR2, 12 MiB embedded preview, 6000×4000 input JPEG, 8 MiB output,
five-minute cooperative deadline, four input numbers per poll. Decode scratch
is 768 KiB, stock and cinema delay lines each have a 384 KiB budget, plus a
128 KiB read header and 8 KiB I/O buffer. Allocation failure aborts cleanly.
Memory budgets and yielding do not guarantee real-camera responsiveness.

## Build on Linux or WSL

Install git, GNU make, gcc and arm-none-eabi-gcc. Obtain a clean checkout of
Magic Lantern at `142472e95054d73640f50789ac0fd66325499d82`, then run:

```sh
python3 src/tools/build-t6.py --ml-source /path/to/magiclantern \
  --output /path/to/new-local-output
```

The script validates the exact commit, model and firmware, archives the pinned
source into a fresh temporary directory, copies this overlay and the current
engine there, and compiles a distinct experiment. Source checkout, boot flags,
camera and memory cards are not modified. `--prepare-only` stages without
compiling. No `--card`, install or flash option exists.

Output includes `AUTOEXEC.BIN`, matching `ML/modules/t6_110.sym`, required bitmap
fonts under `ML/FONTS`, film-stock files, this README, source/artifact hashes,
build log and a manifest. Keep these assets together. These are
an experimental overlay, **not a complete Magic Lantern card distribution**.
Use only a matching, already-working upstream 1300D.110 environment; installation
and physical-camera validation are separate steps. Preserve the original
autoexec and its matching symbol file together for restoration.

Licenses: Soup Engine GPL v2; Magic Lantern GPL v2 or later. See the bundled
LICENSE and upstream source license headers. No warranty.
