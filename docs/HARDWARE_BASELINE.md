# Sanitized T6 hardware baseline

Observed during the 2026-10-31 and 2026-11-01 sessions (America/Los_Angeles).
The USB check below and later physical tests are separate observations.

## Physical still, recording and CPU tests

The owner booted the r11 overlay and produced two 2592×1728 JPEGs. Both decoded
fully; the latest ETERNA/STRONG/BLUE image codestream matched the desktop
reference byte for byte, excluding metadata. See [validation](VALIDATION.md).

With the r12 diagnostic card, the owner ran the CPU test and made two normal
Canon recordings. Read-only transfers of the CSV and both MOVs matched their
card SHA-256 values. The personal media and transfer receipts remain private.

| Saved video | Rate | Decoded frames | Video duration |
| --- | --- | --- | --- |
| 1920×1080 | 30000/1001 (29.97 fps) | 349 | 11.645 s |
| 1280×720 | 60000/1001 (59.94 fps) | 800 | 13.347 s |

Both H.264 streams and their 48 kHz two-channel PCM audio decoded completely without
errors. Every adjacent decoded video timestamp had the expected 1001-tick step
at timebases 1/30000 and 1/60000 respectively. This establishes two short stock
recording baselines with r12 present, not sustained stability, calibrated unique
sensor exposures or advanced effects in saved video. The decoded `yuvj420p`
format does not establish Canon's native pre-encoder input-buffer layout.

The offscreen Strong/Blue packed-YUV test completed one warmup and three timed
calls at each size, using owned **uncached** `fio_malloc` memory:

| Synthetic frame | Raw timed calls (ms) | Median (ms) |
| --- | --- | --- |
| 320×180 | 122, 89, 89 | 89 |
| 640×360 | 390, 388, 359 | 388 |
| 640×480 | 510, 507, 510 | 510 |

The CSV metadata, sample counts and extrema validated independently. Total
elapsed time before freeing memory/report I/O was 4339 ms. Millisecond timings
include scheduling delays, and three samples are not a sustained worst-case
bound. This build uses the size-optimized kernel and uncached pixels/scratch;
these results are not a maximum CPU capability or recording-FPS measurement.
The current path exceeds a 29.97/59.94 fps frame interval even at the smallest
tested size. Cache and compiler effects required the separate comparison below.

### Physical r13 cached/compiler comparison

The subsequent version-2 report reached its cooperative deadline at 10,012 ms
and saved valid partial results. Both allocation classes validated. It retained
26 timed calls, nine warmups and 23 successful full-frame byte comparisons.
The final optimized/cached case has two timed samples; its third is explicitly
missing. The earlier version-1 CSV remained byte-for-byte unchanged.

| Synthetic frame | Size-optimized, uncached (ms) | Size-optimized, cached (ms) | `-O2`, cached (ms) |
| --- | --- | --- | --- |
| 320×180 | 121, 89, 122 | 45, 45, 45 | 38, 39, 39 |
| 640×360 | 389, 389, 389 | 181, 180, 181 | 187, 187, 187 |
| 640×480 | 509, 509, 507 | 241, 273, 241 | 237, 238, missing |

Cached access substantially reduced this workload's elapsed time. `-O2` did
not improve every case, and these few samples do not establish sustained
worst-case timing. Every retained candidate output had its matching byte check;
the global comparison-complete flag correctly remains zero because the last
requested call did not run. These are synthetic kernel timings with no encoder,
sensor or recording-FPS measurement. A cached pointer is not a demonstrated
ownership/cache-handoff policy for Canon's DMA buffers.

### Physical r14 HEVC codec proof

The owner returned one `HVCT0001.HVC` after the manual test. Its read-only copy
matched the card SHA-256, and the installed r14 manifest/binary hashes matched
the expected package. FFprobe identifies HEVC, planar 8-bit 4:2:0, 320×180 and
two frames. FFmpeg fully decoded it without errors; all 172,800 visible sample
bytes match the independently generated expected patterns. The 184,649-byte
codestream is identical to the emulator's equivalent file, with CRC32
3,035,130,369. This establishes that the physical T6 executed our software
encoder and saved the synthetic proof.

Only the small file was present. The owner clarified that both larger sizes
showed the cooperative 30-second limit. Neither a 4K nor an 8K HVC was published.
The old build did not export phase telemetry, so the card does not establish
which phase consumed the time. Its timestamps and decoder-default presentation
rate are not capture-FPS measurements. These are generated pixels, with no live
sensor source or audio.

### Physical r15 4K/8K synthetic HEVC and phase reports

The returned card contained three new HVCs and three corresponding CSVs.
Read-only source/host SHA-256 checks agreed. Independent FFprobe/FFmpeg checks
identified two frames per HVC and fully decoded every frame without errors.
Every visible sample matched an independently generated test pattern. All
three codestreams also matched their equivalent emulator files byte for byte.
Report sizes, CRCs, CTUs, completed phase masks and I/O counters agreed with the
actual files. Personal transfers and full receipts remain private.

| Synthetic size, two frames | HVC bytes | Total ms | Encode/write ms | TMP verification ms | Final verification ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 320×180 | 184,649 | 261 | 151 | 51 | 50 |
| 3840×2160 | 25,100,251 | 32,105 | 17,376 | 7,348 | 7,372 |
| 7680×4320 | 99,662,491 | 128,001 | 69,495 | 29,236 | 29,261 |

Setup/rename and time outside the phase counters account for the remaining
milliseconds. The encoding phase includes test-pixel generation, codec work,
writes, CRC and scheduler yields. Verification consumed about 46% of each
large run. These timings cannot isolate CPU from storage, establish a measured
r14-to-r15 speedup, or measure sensor recording FPS. The files contain generated
pixels with no sensor capture, audio or declared presentation rate. Decoder
fallback FPS is not a measurement. No card software or boot flags were changed
during this read-only verification.

## Earlier read-only USB identity check

| Item | Observed result |
| --- | --- |
| Manufacturer | Canon Inc. |
| Camera model | Canon EOS Rebel T6 |
| USB vendor/product | `04A9:32B4` |
| DeviceInfo firmware string | `3-1.1.0` |
| Canon firmware target | 1.1.0, matching the experimental `1300D.110` target |
| Windows transport | Native WPD, existing installed driver |
| WPD handle access | `GENERIC_READ` |
| Command IOCTL access | `FILE_READ_ACCESS` |
| Camera operation sent | Standard PTP GetDeviceInfo (`0x1001`), no parameters |
| Magic Lantern PTP extension `0xA1E8` | Not advertised in the returned operations list |

The camera became visible to Windows after Wi-Fi/NFC was disabled. Canon's
[T6/1300D connection guidance](https://www.canon.com.au/support/sims-content?cid=34641904BC784248BB738669FF04E83E&ctype=ts&pid=1f51a2dcb60d46559562084a4f1b5ea8)
documents that enabled Wi-Fi/NFC prevents a computer USB connection.

The client sets the documented
`WPD_API_OPTION_IOCTL_ACCESS=FILE_READ_ACCESS` option and successfully queried
the camera using a `GENERIC_READ` handle. Omitting the request-level IOCTL
option initially returned Windows `E_ACCESSDENIED`. The published tool has no
broader-access mode or automatic permission fallback.

The query did not enumerate or download photographs, read ROM/RAM, enable remote
or event mode, invoke EventProc, change camera properties or boot flags, install
drivers, or install/boot the experimental camera overlay. No serial number,
complete device identifier, raw DeviceInfo payload or private photograph is
included in this document.

Absence of advertised `0xA1E8` is a protocol observation. It does not establish
whether a camera boot flag is set, whether some other Magic Lantern build is on
a card, whether undocumented commands exist, or a fundamental hardware limit.
No supported ML memory-read path was established by this check.

See [ROM acquisition status](ROM_ACQUISITION.md) for the separate official-dumper
download inspection and spare-card preparation.

Still unverified on the physical camera:

- Full control coverage, automatic processing and sustained overlay operation.
- DryOS exclusive-open and rename collision behavior.
- Pre-encode movie buffer ownership, saved-video effects and sustained recording
  behavior under experimental code.
- Any experimental capture/frame rate, sustainable processing throughput,
  duplicated/dropped frame behavior, audio drift or thermal limit.

The earlier USB check did not measure recording modes. The later clips above
confirm their saved cadence for those short runs and establish no higher FPS
ceiling. See
[HIGH_FPS.md](HIGH_FPS.md) and [VIDEO_RESEARCH.md](VIDEO_RESEARCH.md).

The [camera-info tool](../tools/read-only-camera-info/README.md) documents the
fixed request allowlist, redaction and offline parser checks. Microsoft documents
the separate [client handle access](https://learn.microsoft.com/en-us/windows/win32/wpd_sdk/client-properties)
and [SendCommand IOCTL access](https://learn.microsoft.com/en-us/windows/win32/api/portabledeviceapi/nf-portabledeviceapi-iportabledevice-sendcommand)
used for this successful read-only path.
