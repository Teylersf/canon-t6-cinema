# Isolated MLinstall WPD enumeration repair

This patch repairs a confirmed Connect crash in the official MLinstall 1.1.2
Windows executable. It does not modify the published r11 camera payload, install
camera firmware, change boot flags, or change the PTP transport.

Apply only to an isolated source archive with these explicitly selected revisions:

| Source | Revision |
| --- | --- |
| MLinstall | `1c78f2639ac8d98b63c296592b6492f6597b84af` |
| `libs/libpict` | `2506c238ea0053d5cd8b12d5615077f8550b0c69` |
| libwpd build dependency | `1b523de4df5fdda15db2ac74ef176c227d668299` |
| libui build dependency | `a577e1e53da0afd1a79d2eacbf7e44173444ff38` |

The parent repository's libwpd gitlink remains
`bd96e1c06186cf784a4c02f03f11b364d38a8da8`; it is provenance, not a usable
complete GUI build dependency. Building against that old archive produces an
undefined `wpd_close` reference from libpict. The chosen March/April 2025 libwpd
history adds the required close function and fixes buffer-resize bookkeeping.
`1b523de` was the latest revision before the June 18, 2025 official executable's
build timestamp. This historical selection does not prove the exact dependency
used by the official binary, whose source recipe fetched an unpinned branch.
The chosen and gitlink revisions have identical `src/parse.c` contents.

The patch paths are relative to the MLinstall source root:

```
git apply --check /path/to/skip-unsupported-wpd.patch
git apply /path/to/skip-unsupported-wpd.patch
```

Use LF source archives. Windows CRLF working files are not the build input.
Pin the complete build dependencies explicitly: upstream's libpict CMake recipe
otherwise fetches libwpd `master`, and the GUI recipe can fetch libui `master`.
Use `FETCHCONTENT_SOURCE_DIR_LIBWPD` and `FETCHCONTENT_SOURCE_DIR_LIBUI` to point
at these exact extracted archives. Preserve the upstream checkouts/gitlinks;
the compatible libwpd archive overlays only the isolated build stage. The patch
does not alter those recipes or imply binary reproducibility.

## Confirmed failure

The Windows crash offset `0x1A79` is immediately after `abort()` in MLinstall's
`ptp_panic` implementation. The official executable also contains the path from
device enumeration to `ptp_panic("wpd_parse_io_path")` on any path-parser error.

During a separate Windows WPD Manager enumeration, the first three entries had
non-USB paths rejected by the exact upstream parser. The fourth was the Canon
T6 and parsed successfully. That offline parse check sent no PTP commands. The
installer therefore aborted before opening the camera. No complete device IDs,
serial numbers, crash dumps, or private enumeration output are published here.

## Change and limits

The enumerator now skips unsupported and null path entries, allocating a list
node only after successful parsing. Supported USB entries retain their original
order and IDs. No parser case policy changes are needed for the observed T6.
Connect now returns an opening error before attempting session or device-info
operations; the previous source overwrote that error.

An allocation failure frees partial list nodes and the WPD ID allocation. An
empty or entirely unsupported result releases the unused WPD allocation. For a
successful result, this minimal patch preserves the pinned library's existing
borrowed-path lifetime: returned node handles refer to the WPD ID allocation.
The upstream application retains that successful enumeration list, and its
free-entry callback does not release those paths. This patch does not redesign
that ownership or fix the existing successful-connection leak. It never logs IDs.

The original strict path parser remains unchanged. Unsupported devices are
ignored rather than treated as a fatal camera protocol error. No IOCTL access,
permission fallback, command allowlist, or camera operation is changed here.

The full GUI Connect action is broader than the separate read-only identity
tool: after device info it sets EOS remote/event modes and reads events/build
version. Boot-related EventProc actions are separate button callbacks. These
offline tests invoke neither GUI controls nor a camera.

## Offline verification

```
python3 test_patch.py --source /path/to/pinned/mlinstall
python3 test_patch.py --source /path/to/pinned/mlinstall --sanitize
```

The tests verify the three original checkout revisions and extract the chosen
libwpd build dependency by its exact commit, without moving its old gitlink
checkout. That build commit must be available in the libwpd repository's object
database. They verify the unchanged parser and the required close API, extract
exact Git blobs into a temporary directory, apply this patch, and compile its
actual enumeration and Connect functions with that parser and mocked WPD/PTP
services. They check
unsupported entries first/middle/last/all, unchanged valid USB order, an empty
list, failures on the first and second node allocation, and Connect's opening,
session, device-info, and success paths. Lock balance, list linkage, borrowed
handle lifetime, cleanup and stopping before session operations on open failure
are checked. The harness explicitly releases the application's retained-success
fixtures so sanitizer leak checks do not hide test-owned allocations.

Strict host and ASan/UBSan checks pass. Native GUI compilation and any subsequent
camera operation are separate validation steps; offline mocks do not prove them.

A separate local build using the explicit dependency pins above subsequently
connected to the verified Rebel T6 firmware 1.1.0. Its GUI reported success for
one `EnableBootDisk` action and then an intentional disconnect. The owner
reported the ML splash/menu after normal power cycling with the prepared card.
No independent persistent-flag readout, physical photo-processing result or
video/FPS result follows from the installer response alone.
