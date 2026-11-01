# T6 Cinema — experimental 1300D.110 integration

**HARDWARE UNTESTED.** Compiling this source is not evidence that it boots or
operates reliably on a physical camera. Target: Canon EOS Rebel T6 / EOS 1300D /
Kiss X80 with Canon firmware **1.1.0 only**. T6i/750D, T5/1200D and firmware
1.2.0 are different targets. No installer or boot-flag writer is supplied.

This adds a **T6 Film** page to Magic Lantern's existing menus. It retains the
pinned upstream 1300D platform, entry points and original limited features.
There are no borrowed 5D/50D ROM addresses, replacement GUI hooks, sensor-mode
changes, RAW video unlocks, or live video effects.

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
- **Cinema effects:** OFF, Subtle, Cinema, Strong. **Streak tint:** Blue or Amber.
- **Cancel / stop auto:** disables automatic processing and cancels at a row or
  I/O boundary. Shutting down also requests cancellation.

Output beside `IMG_1234.CR2`: `T6F_1234.JPG`, via a new `T6F_1234.TMP`.
The worker refuses detected existing output or temporary files. It requests
temporary creation with `FIO_OpenFile(O_WRONLY | O_CREAT | O_EXCL)`. A rejected
exclusive open aborts the job; there is no fallback to `FIO_CreateFile`, whose
upstream implementation removes existing paths. Only a temporary file opened
by the current job can be cleaned up. **DryOS exclusive-create flag behavior
has not been validated on a physical 1300D.110**; desktop mock behavior is not
proof of those firmware semantics. A successful job checks encoded length and
JPEG start/end markers before publishing its output.
The destination is rechecked before native `FIO_RenameFile`. Its behavior if
another writer creates the destination after that check remains unverified on
this camera; publication is not claimed to be an atomic no-overwrite operation.
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

Output includes `AUTOEXEC.BIN`, matching `ML/modules/t6_110.sym`, film-stock
files, this README, source/artifact hashes, build log and a manifest. These are
an experimental overlay, **not a complete Magic Lantern card distribution**.
Use only a matching, already-working upstream 1300D.110 environment; installation
and physical-camera validation are separate steps. Preserve the original
autoexec and its matching symbol file together for restoration.

Licenses: Soup Engine GPL v2; Magic Lantern GPL v2 or later. See the bundled
LICENSE and upstream source license headers. No warranty.
