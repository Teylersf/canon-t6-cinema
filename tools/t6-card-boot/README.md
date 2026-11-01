# Local T6 card boot-marker utility

This Windows C++ utility is deliberately restricted to the reviewed **H:**
removable card with physical capacity **15,931,539,456 bytes**. It cannot select
another drive letter or a physical disk number. It does not change the camera's
BOOT flag, install a driver, format a card, change partitions, delete files, or
request elevation. It is unsigned source-built engineering tooling.

The default operation is inspection, using read-only handles. A physical-card
inspection was attempted during the root workflow and Windows denied raw
access with error 5. No card-sector write was attempted.

## Source and Windows API basis

- Magic Lantern commit `142472e95054d73640f50789ac0fd66325499d82`,
  [`src/bootflags.c:167–170`](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/bootflags.c#L167):
  FAT32 uses `EOS_DEVELOP` at byte `0x47` (11 bytes) and `BOOTDISK` at `0x5c`
  (8 bytes). This tool changes exactly those ranges in each boot sector.
- The pinned ML FAT32 routine updates the primary boot sector only. This tool
  also updates the validated backup named by `BPB_BkBootSec` (BPB byte `0x32`),
  addressing the inconsistency reported in [upstream issue 314](https://github.com/reticulatedpines/magiclantern_simplified/issues/314).
- FAT32 BPB fields `info_sector` and `backup_boot`, and the two FSInfo
  signatures, are also defined by the [Linux FAT on-disk header](https://github.com/torvalds/linux/blob/master/include/uapi/linux/msdos_fs.h).
  Primary FSInfo is at `BPB_FSInfo` (`0x30`); its backup is at
  `BPB_BkBootSec + BPB_FSInfo`. Both must be inside the reserved area. They are
  read and backed up, never modified.
- [Volume disk extents](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-ioctl_volume_get_volume_disk_extents)
  identify the disk belonging to H:. Storage descriptors, drive layout,
  disk attributes, logical sector size and writable status are checked.
- A successful [FSCTL_LOCK_VOLUME](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_lock_volume)
  is mandatory before any raw write. Lock failure has no fallback. The locking
  handle performs the writes; closing it releases the lock.

## Build and fixture-only CI

Run from the repository root with Visual Studio C++ Build Tools and Windows SDK:

```powershell
.\tools\t6-card-boot\build.cmd test
.\tools\t6-card-boot\build.cmd test-asan
```

Both compile with `/W4 /WX`; the second also instruments the fixture test with
MSVC AddressSanitizer. They execute **only `card-fixture-tests.exe`**. The utility
that accesses H: is compiled but never launched by either build/test command.
No administrator privileges or attached camera/card are required for tests.

Fixtures include a valid FAT32 sparse memory image and a real sparse regular
file with the reviewed 16 GB logical size. Tests cover exact marker-only writes,
unchanged MBR/FSInfo/data, malformed MBR/signatures/partition scope, BPB geometry,
sector size, backup offsets/mismatch, both FSInfo records, removable/system/
readonly/context refusals, strict manifest parsing, FAT mirrors, missing,
oversized, duplicated, truncated and cyclic executable chains, and a changed
sector detected immediately before writing. All preflight refusals leave the
fixture bytes unchanged. A separate injected midwrite failure verifies that
partial state is surfaced and no retry or rollback occurs.

## Read-only inspection

```powershell
.\tools\t6-card-boot\build\t6-card-boot.exe --inspect
```

No arguments has the same effect. Output is a small JSON report with the
observed extent and an identity token. The token combines the current H:
volume GUID, storage descriptor, disk number/capacity/extent, MBR and validated
boot/FSInfo sectors. It is an internal stale-device guard for the calling
workflow; it is not an additional user confirmation step. Inspect after copying
the reviewed package, because file writes can legitimately change FSInfo.

Inspection refuses a card that is already or partially marked, inconsistent,
not exactly the reviewed capacity, not removable USB/SD/MMC, readonly/offline,
on the Windows system disk, not one MBR partition/one extent, not logical-512
FAT32, or not labeled `EOS_DIGITAL`. BPB and Windows partition extents must
agree exactly. Primary and backup boot sectors must match byte for byte.

## Explicit marking operation

The root workflow must first review the code/build and finish its emulator
checks. These are the arguments for the later authorized marking operation;
they were not executed during implementation:

```text
t6-card-boot.exe --mark --identity <current-inspection-token> --package <host-package-directory> --manifest-sha256 <reviewed-manifest-hash>
```

The host package must be outside H: and contain `manifest.json` and
`AUTOEXEC.BIN`. The SHA-256 pin must match the manifest bytes; the manifest must
declare `1300D.110`, Canon `1.1.0`, the pinned ML commit, and the matching
AUTOEXEC hash. H: must already contain the identical manifest. The tool does
not copy package files. After obtaining the exclusive lock it revalidates the
device and sectors, reads root `AUTOEXEC.BIN` directly through the FAT32 chains,
and checks its size/hash against the source package.

Before marking, it creates an exclusive folder below
`%LOCALAPPDATA%\T6Cinema\card-backups\{GUID}` with a protected ACL limited to the
current user and SYSTEM. It saves the original MBR, both boot sectors, both
FSInfo sectors and a private identity/package record; flushes and verifies the
five original-sector files. The backup directory is outside the Git workspace
and H:. Existing backup files are never overwritten.

Only the backup boot sector and primary boot sector can be written. Each is a
512-byte copy of its original with the two marker strings substituted. Each
write is flushed and verified, then both sectors and unchanged MBR/FSInfo are
checked. No retry, automatic rollback, or formatting occurs.

Two sector writes are **not atomic**. Exit code 2 means a write was attempted
and partial state may exist: retain the private originals and inspect before
any further operation. Exit code 1 means the utility attempted no card-sector
write. Windows may independently flush its existing filesystem cache when a
volume lock is acquired. Neither a backup nor a successful readback proves
resilience to a later power loss or unsafe card removal.

Only boot-marker bytes change, so the root directory's `EOS_DIGITAL` label is
preserved while the BPB marker becomes `EOS_DEVELOP`. Filesystem repair tools
may report that label difference; changing those markers can undo ML booting.
This tool does not attempt a filesystem repair or an unmark/restore operation.
