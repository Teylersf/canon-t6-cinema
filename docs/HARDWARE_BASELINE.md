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
tested size. Cache and compiler effects require separate physical comparisons.

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
