# Sanitized T6 hardware baseline

Observed during the 2026-10-31 session (America/Los_Angeles). This records a
bounded USB identity/protocol check, not a camera-firmware or video benchmark.

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

Still unverified on the physical camera:

- Booting and operating the T6 Cinema overlay, its menus and still-image worker.
- DryOS exclusive-open and rename collision behavior.
- Pre-encode movie buffer ownership, saved-video effects and normal recording
  behavior under experimental code.
- Any experimental capture/frame rate, sustainable processing throughput,
  duplicated/dropped frame behavior, audio drift or thermal limit.

The published Canon recording modes remain manufacturer specifications; this
USB check does not measure them or establish a higher FPS ceiling. See
[HIGH_FPS.md](HIGH_FPS.md) and [VIDEO_RESEARCH.md](VIDEO_RESEARCH.md).

The [camera-info tool](../tools/read-only-camera-info/README.md) documents the
fixed request allowlist, redaction and offline parser checks. Microsoft documents
the separate [client handle access](https://learn.microsoft.com/en-us/windows/win32/wpd_sdk/client-properties)
and [SendCommand IOCTL access](https://learn.microsoft.com/en-us/windows/win32/api/portabledeviceapi/nf-portabledeviceapi-iportabledevice-sendcommand)
used for this successful read-only path.
