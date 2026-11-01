# T6 native file creation and collision tests

Target: EOS Rebel T6 / EOS 1300D, Canon firmware **1.1.0**.
Assessment: 2026-10-31, America/Los_Angeles. Magic Lantern source:
`142472e95054d73640f50789ac0fd66325499d82`.

These results concern a private QEMU instance running the camera's ROM code.
They do not establish physical-camera reliability, media behavior or timing.
Original ROM dumps and the physical memory card were not modified. Firmware
bytes, disassembly, private hashes and debugger logs remain outside Git.

## Why the original exclusive-open request failed

The pinned [ML file-I/O header](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/fio-ml.h#L54)
defines `O_WRONLY | O_CREAT | O_EXCL` as `0xC1`. In this T6 ROM's native path,
that combination fails to create an absent temporary file: the native function
returns `-1`, and [ML's wrapper](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/fio-ml.c#L370)
converts it to `NULL`. The actual worker had already parsed the CR2 preview and
stock, calculated dimensions and initialized its processing buffers. The
failure was not a JPEG parser or allocation error.

Read-only ARM control-flow analysis identified the following roles in this
specific native file-system path. These are inferred implementation roles,
not a portable POSIX contract or Canon-published API:

| Bit | Observed role |
| --- | --- |
| `0x0001` | Write access |
| `0x0100` | Enter the creation path |
| `0x0200` | Truncate an existing file; excluded from the worker request |
| `0x0400` | Preserve an already-exists error instead of opening the existing file |
| `0x8000` | Check for an existing entry before creation |

The tested create-new combination is **`0x8501`**. ML's existing native wrapper
adds its internal `0x10000` bit; the file-system driver was observed receiving
`0x18501`. The two creation/collision bits are both necessary in the traced
path. `0x501` created a new file but also opened a competing existing file in an
earlier virtual test, so it does not provide the required behavior.

The native entry point and [ML wrapper signatures](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/fio-ml.h#L146)
accept a filename and unsigned mode and return a file handle. The
[T6 stubs](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/platform/1300D.110/stubs.S#L45)
identify the native open, close, write and rename functions used by the tests.
The new flag combination must remain scoped to this exact T6 firmware; it is
not justification for changing global ML flags or another camera's interface.

Neither create API is an acceptable fallback. ML's `FIO_CreateFile` wrapper
removes an existing pathname before creation. The raw Canon creator reaches
native open with `0x301`, which includes truncation and does not supply the
tested collision checks.

## Virtual test method

The test used the compiled r6 worker and writable, private qcow2 images derived
from a clean virtual-card baseline. The input was the public CC0 T6 CR2 fixture
under a Canon-shaped DCIM filename. Canon's emulator selected a different
current folder, so only ML's in-memory SD-card folder field was synchronized
with the imported fixture folder. This was a fixture injection, not evidence
of capturing a photograph inside the emulator.

For these diagnostic runs, GDB changed the mode register from `0xC1` to `0x8501`
immediately before the worker's native open call. No production C source or
binary was patched. Separate test output names were substituted in the worker's
stack buffers for collision cases. Competing files were created, written and
closed through native file APIs in the paused worker context, after the final
existence check and before the tested operation. Internal breakpoints were
disabled during those injected calls.

The subsequent production build (r7) completed the same job using its compiled
T6-scoped `0x8501` constant, without a debugger file-mode override. After a
normal emulator shutdown, an independent export decoded the entire RGB
2592×1728 JPEG, verified its 678,863-byte length and byte equality with the
diagnostic result, and verified the original CC0 source hash. The fixture-folder
injection described above was still necessary. This establishes a production
worker run in emulation, not physical-camera operation.

## Observed results

| Case | Native result | Verified outcome |
| --- | --- | --- |
| Absent temporary and destination | New handle; worker returned success | Separate 678,863-byte JPEG; all 1,728 output rows completed |
| Competing temporary inserted after the final check | Creation primitive returned `-17`; errno `17`; wrapper returned `NULL` | Eight-byte `RACETMP!` remained unchanged; worker created no JPEG and did not remove the competing temporary |
| Competing destination inserted after its final check, before rename | Native rename returned `-1` | Eight-byte `RACEOUT!` remained unchanged; only the worker-owned temporary was removed |

After native handles closed, the virtual machine was briefly paused and its
private SD image exported read-only. Independent Pillow decoding loaded the
entire successful JPEG as RGB **2592×1728**. It was byte-identical to the earlier
successful render with the same fixture and settings. The exported source CR2
matched the original public fixture. Inspection of the exported file system
also confirmed both sentinel files after worker cleanup, absence of the failed
job's owned temporary, and absence of a JPEG from the rejected create attempt.

The rename test supports collision preservation for this emulated native path.
It does not prove atomic no-overwrite semantics on physical storage. Keep that
limitation explicit until matching physical tests are complete. No test here
measures capture responsiveness, power-loss recovery, card-controller behavior
or processing throughput on the real camera.
