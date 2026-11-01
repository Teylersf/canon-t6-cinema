# T6 ROM acquisition status

The connected T6 reports Canon firmware **1.1.0**. No ROM dump or camera-side
execution was completed during preparation of this note.

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

The unchanged official dumper was staged on the user's FAT32 spare card and
read back with the same SHA-256. No formatting, deletion, card bootblock change,
camera bootflag change or firmware execution was performed.

Keep resulting ROM files and RESCUE.LOG private and outside Git. Check generated
MD5 files against the actual dumps before emulator work; historical reports
include a ROM0 checksum discrepancy. A successful dump will not by itself
validate saved-video effects or higher capture FPS.
