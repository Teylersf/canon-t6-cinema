# Validation status

Tested on the development computer, not a physical T6. The camera's owner
reported Canon firmware 1.1.0; no camera was connected for the initial build.

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

The initial reference produces 2592×1728 output from a real 1300D embedded JPEG.
No desktop timing is presented as a T6 processing-speed measurement.

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
