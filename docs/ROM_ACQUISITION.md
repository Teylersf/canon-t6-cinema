# T6 ROM acquisition status

The connected T6 reports Canon firmware **1.1.0**. The owner ran the official
portable dumper and returned its output on a 16 GB card formatted by the camera.
Both 32 MiB dumps, checksum files and the rescue log were copied into ignored
private storage and checked against their source-card copies.

**[Download the original ROM1 reference and view its checksums](ROM_DOWNLOAD.md).**
The project owner subsequently explicitly requested public release of ROM1.
Only that original ROM1 and sanitized documentation/checksums are published;
the failed ROM0, rescue log and private analysis remain excluded.

ROM1 matches the camera-generated MD5. ROM0 does not: its entire contents are
one repeated four-byte pattern. ROM0 is not a validated firmware image. See the
[emulator limitations](../integrations/qemu/README.md).

The ML developer's [portable dumper post](https://www.magiclantern.fm/forum/index.php?topic=16534.msg161097#msg161097)
links [DMP1300D.FIR](https://a1ex.magiclantern.fm/debug/portable-rom-dumper/new/DMP1300D.FIR).
[Historical testing](https://www.magiclantern.fm/forum/index.php?topic=16534.msg210726#msg210726)
reports success on 1.1.0 after a T6-specific ROM-through-RAM fix. The following
post says the FIR files were updated.

Observed official download, 2026-10-31:

- Size: **67,804 bytes**.
- SHA-256: `c979b4fe7b606cd576aa1519ea2a7e29a1585fc8ed8d8d08614a6661af61d81a`.
- Container model: `0x80000404` (T6/1300D).
- Container version label: `1.3.3`; this does not identify the installed Canon OS.
- Embedded file length and outer additive checksum matched.

These observations identify the downloaded file, not an independently
authenticated publisher checksum. Its encrypted payload was not successfully
decoded; exact source/binary equivalence and HMAC authentication remain
unverified.

An earlier 32 GB spare card was reformatted after the owner confirmed its data
was disposable, but camera compatibility was not established. The successful
dump used a different 16 GB card formatted in the T6. The unchanged official
dumper was copied to its root and read back with the same SHA-256 before use.
The rescue log reported the camera BOOT flag as zero at acquisition. The film
overlay subsequently booted on the physical camera, and later still processing
and synthetic HEVC tests passed; see [validation](VALIDATION.md).

Keep private rescue logs, failed dumps and device diagnostic records outside
Git. ROM1's owner-authorized reference release is separate from the GPL source
and camera overlay packages. Check generated MD5 files against actual dumps
before emulator work; historical reports
include a ROM0 checksum discrepancy. A successful dump will not by itself
validate saved-video effects or higher capture FPS.
