# Validation status

The owner reports the first physical-camera Magic Lantern splash/menu after
normal startup with the prepared r11 card. Two resulting JPEGs were independently
read back and decoded. Read-only USB DeviceInfo identified the camera as a Rebel
T6 with firmware 1.1.0; its private ROM1 dump matches the camera checksum.

## Established

- Full ARM946E-S camera build against the pinned 1300D.110 platform succeeds.
- Two physical-camera outputs decode fully as RGB 2592×1728 with the expected
  T6 Cinema software tag. The latest matches the desktop ETERNA / STRONG / BLUE
  JPEG image codestream byte for byte, excluding metadata. Both corresponding
  source CR2 previews decode; transfer hashes match the card readback. No source
  hash was recorded before processing, so this is not a before/after hash proof
  of source immutability. Personal photographs and hashes remain outside Git.
- Shared streaming anamorphic effect tests pass strict compilation, AddressSanitizer
  and UndefinedBehaviorSanitizer with recovery disabled.
- CR2/JPEG malformed-input regression tests pass, including real T6 preview
  decoding in memory and through a half-resolution streaming reader.
- Stock tests reject malformed and overflowing numbers before conversion and
  prevent extreme exposure values from overflowing the film-curve calculations.
- Desktop CLI end-to-end tests pass with independent Pillow JPEG decoding,
  shape/effect checks, invalid arguments, malformed headers, existing files,
  same-path inputs, hardlinks and symlinks.
- The actual camera still worker is compiled on the host behind mocked Canon
  APIs. Its real T6 output matches the desktop reference's JPEG image codestream
  byte-for-byte for Portra / Cinema / blue / scale 2 / quality 88. EXIF text differs.
- Worker success, failure, cancellation and contention tests preserve original
  files and existing output files; newly owned partial files are cleaned up.
- The actual T6 ROM reaches Canon's shooting display and our ML menus in QEMU.
  The retro dashboard renders with the pinned bitmap fonts; its emulated dial,
  D-pad, SET, Q and DISP controls alter the expected state.
- Native ROM diagnostic runs establish T6-specific create-new behavior and
  preserve files inserted immediately before creation or rename. Independent
  JPEG decoding and source-file hash verification passed. See
  [native file-I/O evidence](NATIVE_FILE_IO.md) for the diagnostic method.
- Native menu/worker formatting regressions, button release pairing, queued
  menu-close races, cancellation and bounded drawing tests pass sanitizers.
- Native T6 file-size/open probes reject four long recipe filenames while their
  short FAT aliases succeed. Camera packages therefore use stable 8.3 recipe
  filenames and a matching index; desktop source filenames are preserved. The
  final r11 native run loads all thirteen named recipes.
- The final native emulation run closes the dashboard and ML menu on two short
  half-shutter pulses and remains closed after an additional 1.2 seconds each.
  The checked T6 menu-intent patch prevents duplicate close wakes reopening it;
  its actual staged helper/consumer also pass sanitizer scheduling regressions.
- The final emulator button checks also cover all stock dial selections/wrap,
  MENU/AV close and reopen, DISP guide, Q pages, and Playback/zoom/LV console
  exits. These handoffs establish console exit and event forwarding; they do
  not prove full playback, zoom, sensor or encoder behavior. LV reached the
  firmware's Live View state.
- The Windows card utility passes 1,288 fixture checks, including a sparse-file
  integration test and AddressSanitizer. After Windows administrator approval,
  its physical inspection and marking completed successfully: both boot sectors
  read back as planned, with five original sectors backed up privately. All 27
  package/checksum files still matched their hashes afterward.
- The isolated packed-YUV backend passes strict host, sanitizer and ARM946E-S
  compilation checks. It has no encoder-frame hook; its
  explicit ownership flag does not verify producer/encoder/DMA/cache safety.
- The newer diagnostic build links that backend only for a serialized offscreen
  CPU test. Native emulation saved and independently read back a CSV with three
  cases and three valid timed frames per case; cancellation, deadline-partial
  reports, collisions and readback-allocation failures pass host sanitizers.
  Native photo elapsed formatting and shutter closure also passed. Emulator
  durations are not physical CPU or recording-speed measurements.
- The build checks the complete static ARM ELF load, including BSS, against
  `[0xC80000,0xD00000)`. The diagnostic image uses 462,848 bytes and leaves
  61,440 bytes in that reserve. Heap allocations and task stacks are separate.
- The isolated symbolic video observer passes strict host tests, ASan/UBSan
  and freestanding ARM946E-S compilation. Tests cover storage boundaries,
  fixed field IDs, paired changes/failures, clock wrap, guard precedence,
  deadlines and late ticks without catch-up. It is excluded from camera
  builds and has no native address map or hardware observation result.

The initial reference produces 2592×1728 output from a real 1300D embedded JPEG.
No desktop timing is presented as a T6 processing-speed measurement.
Emulation uses a derived ROM1 reset sector and an explicitly unvalidated ROM0
mapping. It is startup/UI/API evidence, not a sensor or recording benchmark.

The [source checks for commit 25ede55](https://github.com/Teylersf/canon-t6-cinema/actions/runs/41284484532)
passed both Windows and host/ARM jobs, including the synthetic CPU benchmark,
static-RAM guard and staged menu-intent test. The r12 card update backed up the
six changed software files and read back all 27 package files successfully;
the existing test photos, RAW files and settings retained their hashes.
The owner's physical CPU report and unchanged 720p60 MOV are still pending.
The first physical-card copy stalled with Windows disk event 153 I/O retries.
After reconnecting the reader, the complete package copied and verified. The
reviewed helper then marked the card. Official MLinstall crashed during device
enumeration; the [source repair](../integrations/mlinstall/README.md) skipped
unrelated WPD entries. Its locally built GUI connected to the verified T6 and
reported success for one `EnableBootDisk` command, followed by intentional USB
disconnect. The owner then reported the ML splash/menu after power cycling.
This is initial boot evidence, not an independent persistent-flag readout or
validation of all physical controls or the video pipeline. The later JPEG
readback establishes the narrower manual still-processing result above; no
physical processing-duration measurement has yet been recorded.

## Not established

- Sustained physical operation, Canon scheduling and cache behavior.
- Camera playback indexing, responsiveness, sustained processing or battery life.
- Reliable simultaneous shooting and background processing. Automatic mode is
  experimental and starts OFF; movie mode and capture activity abort still jobs.
- Anamorphic effects in saved video, a safe pre-encoder frame hook, real-time
  effects, RAW video, higher recording FPS, or an optical anamorphic field of view.
- Full sensor-resolution outputs or full sensor-RAW image development.

## Reference fixture

Optional, excluded from Git:

- Dataset: [raw.pixls.us](https://raw.pixls.us/), entry 1555, Canon EOS 1300D RAW (3:2).
- License: [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
- Size: 22,246,213 bytes.
- SHA-256: `e064327405b3a736b1cc89d499bc5a049cfb2800bcb06a5072e6d0a82cd296d7`.
- Fetch with `python3 src/tools/fetch_t6_sample.py`.

Synthetic fixtures and malformed-header tests are generated locally. Do not
commit personal photographs, Canon ROM dumps, serial numbers or device logs.

## Inherited behavior retained

Stock names and rendering are inherited creative approximations. One known
upstream color-math issue remains: `look.c` initializes its sine-table angle
using `i & 1535`, although 1536 is not a power of two. This can affect some hue
rotations; changing the stock renderer is deferred rather than silently claiming
colorimetric fidelity. The stock/parser and encoder memory/undefined-behavior
fixes required for this prototype are included.
