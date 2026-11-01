# Download: Canon T6 / 1300D / Kiss X80 ROM1 — firmware 1.1.0

**[Download the original ROM1 dump ZIP](https://github.com/Teylersf/canon-t6-cinema/releases/download/rom-1300d-110-reference-v1/t6-1300d-110-original-rom1.zip)**

[Release page and checksums](https://github.com/Teylersf/canon-t6-cinema/releases/tag/rom-1300d-110-reference-v1)
· [Machine-readable reference manifest](firmware/1300d-110-rom1.json)

This is the unchanged ROM1 dumped from the project owner's **Canon EOS Rebel
T6 / EOS 1300D / Kiss X80 running firmware 1.1.0** on 2026-10-31. Its MD5 matches
the camera-generated checksum. It is provided as a firmware research and
emulator reference. **This is not a Canon `.FIR` update, a Magic Lantern
installer, or a file to flash to a camera.** Downloading it does not install
the T6 Cinema overlay. For that separate build, see the
[camera overlay instructions](../README.md#build-the-experimental-camera-overlay).

## Verify the download

The archive contains only `ROM1.BIN`, `ROM1.MD5`, `SHA256SUMS`, this guide as
`README.md`, and `manifest.json`. The failed ROM0 dump, rescue log, private
analysis and camera media are excluded.

| Original ROM1 | Value |
| --- | --- |
| File size | 33,554,432 bytes (32 MiB) |
| SHA-256 | `894ffd8c6e4ec7766754278e662899ffee06d62aaaf558f0836f6e771ade46db` |
| MD5 | `01249972ece413315cbfac847a90fac7` |
| Camera MD5 matches | Yes |
| Final 64 KiB | Entirely `FF`; original bytes preserved |

After extracting the ZIP, run either:

```powershell
Get-FileHash .\ROM1.BIN -Algorithm SHA256
```

```sh
sha256sum -c SHA256SUMS
```

Compare the SHA-256 with the value above. The release also includes a separate
`SHA256SUMS` for the ZIP itself. A checksum establishes byte identity, not
publisher signing, a complete hardware-ROM acquisition or safe flashing.

## Use for analysis and emulation

Analyze a read-only copy. The observed ROM1 mapping uses base `0xFE000000`;
offset `0x000C0000` corresponds to address `0xFE0C0000`. Other aliases are an
emulator/model detail. Target the **1300D.110** platform; T6i/750D, T5/1200D and
other Canon firmware versions are different targets.

The original's final 64 KiB reset bank is blank. Our emulator tests used a
**separate reconstructed copy**, copying `[0x10000:0x20000]` into
`[0x1FF0000:0x2000000]` following the
[historical developer workaround](https://www.magiclantern.fm/forum/index.php?topic=16534.msg210728#msg210728).
The original dump in this download has not been changed. The following local
Python example makes a new diagnostic copy and refuses an existing output:

```python
from pathlib import Path
import hashlib

original = Path("ROM1.BIN").read_bytes()
assert len(original) == 33554432
assert hashlib.sha256(original).hexdigest() == (
    "894ffd8c6e4ec7766754278e662899ffee06d62aaaf558f0836f6e771ade46db"
)
assert original[-65536:] == b"\xff" * 65536
derived = bytearray(original)
derived[-65536:] = original[0x10000:0x20000]
with Path("ROM1-qemu.BIN").open("xb") as output:
    assert output.write(derived) == len(derived)
```

This reconstruction is for emulator diagnostics. It is not a repair image for
physical hardware. ROM1 alone is **not a complete validated emulator ROM set**:
the project owner's ROM0 failed its camera checksum and contains one repeated
word, so it is not included. Our emulator additionally used an explicitly
unvalidated ROM0 diagnostic accommodation. Read the
[complete emulator limitations and setup](../integrations/qemu/README.md)
before interpreting a successful boot or UI test. Emulation does not establish
sensor readout, recording FPS, hardware timing or camera safety.

## Provenance and rights

The project owner explicitly requested publication of this dump from their
camera. The bytes contain Canon firmware, which is proprietary and **is not
relicensed under this repository's GPL**. Canon and other respective owners
retain their rights; this repository does not grant rights in their code.
This is not an official Canon distribution or an endorsement by Canon.

It is an original dump from one physical device. No device-specific personal
value was identified in the existing analysis, but the file has **not been
certified free of device-specific state**. Private rescue logs, identifiers
from other diagnostic outputs and personal media are not included.
