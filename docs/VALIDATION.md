# Validation status

The film overlay has not been tested on a physical T6. The owner's camera was
identified through read-only USB DeviceInfo as firmware 1.1.0 and supplied a
private ROM1 dump with a matching camera-generated checksum.

## Established

- Full ARM946E-S camera build against the pinned 1300D.110 platform succeeds.
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
- The Windows card utility passes 1,288 fixture checks, including a sparse-file
  integration test and AddressSanitizer. Its physical read-only inspection was
  denied by Windows with error 5; no card-sector write was attempted.

The initial reference produces 2592×1728 output from a real 1300D embedded JPEG.
No desktop timing is presented as a T6 processing-speed measurement.
Emulation uses a derived ROM1 reset sector and an explicitly unvalidated ROM0
mapping. It is startup/UI/API evidence, not a sensor or recording benchmark.

## Not established

- Boot or operation on physical hardware, Canon scheduling and cache behavior.
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
