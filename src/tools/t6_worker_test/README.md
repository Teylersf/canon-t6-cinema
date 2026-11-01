# Camera worker regression model

Run `make test` or `make asan` in this directory. The harness includes the actual
`1300D.110/src/film_develop.c`, links the shared image engine, and uses the public
T6 fixture with in-memory DryOS/file API replacements. It never accesses a camera.

The mock intentionally reproduces upstream `FIO_CreateFile` deleting an existing
path. Tests prohibit using that API for output. For `FIO_OpenFile` they independently
require literal **0x8501**, the actual 1300D.110 write/create/exclusive/check-existing
combination. They reject the old global-macro combination 0xC1 and the lookup-missing
0x501 without creating a file. The global ML macros remain unchanged. Tests inject
a competing TMP inside the open after the last existence check and check that its
bytes survive. An injected API rejection must fail without fallback. A destination
appearing inside rename is also refused with all eight original bytes preserved.

The native flag and rename contract is now backed by actual-firmware emulator
tests: new output completes a 1728-row valid JPEG, a late TMP collision returns
EEXIST (17) preserving eight bytes, and a late destination collision makes native
rename return -1 preserving eight bytes. See `docs/NATIVE_FILE_IO.md` in the
repository. The host harness checks the worker against that independently observed
contract. This is **emulator-tested, physical-camera untested**; capture scheduling,
real media, cache/DMA behavior and physical-device responsiveness still require
validation. Neither test claims every publication path is atomic.

Other checks cover partial reads, output collisions, canceled/overdue jobs,
shutdown, capture becoming busy, write/read failures, rename failure, changing
source size, malformed input, original preservation, and handle cleanup.

The native-menu and worker tests reject unsupported format constructs before
calling host libc. The pinned ML `snprintf` wrapper delegates to Canon ROM's
nonstandard `vsnprintf`; QEMU exposed unsupported string precision (`%.34s`,
`%.31s`). Tests verify exact stock filenames, explicit bounded status clipping,
and rejection of precision/star/length/float formats. This restricted contract
is not an emulation of every Canon formatter behavior.

Retro rendering stubs require `NO_BG_ERASE` in the native font background byte;
transparent/black background erasure would destroy selected amber rows. A
modeled native half-shutter chain checks that the addon contributes no second
close signal, including half-state polling, busy-before-halfpress polling, and a
busy OLC notification preceding halfpress. This targeted adapter model does not
execute the native redraw task; the separate staged-code test below covers that
additional close producer and its consumer.
An AV press/release pair queued before native menu-task processing is also
required to post only one close toggle, including when the menu remains visible
until both input events have arrived.
The r7 emulator also exposed an undefined type-0 notification (observed code 76)
after the half-press state changed but before the native half-press event (72).
The regression reproduces that order and checks that the native handler remains
the only close producer. The production gate gives capture precedence whenever
half-press is already active; it adds no definition or special handling for 76.
ISO and AV handoff during that same interval are checked as well.

The r10 emulator traced another native producer: the menu redraw task can close
on the same half-press as the key handler. Both formerly signaled an untyped
toggle, allowing close followed by reopen. The checked T6-only archive patch
now records an absolute desired menu state before waking its consumer. Repeated
close requests and delayed wakes cannot toggle a closed menu open. Native MENU
and half-press request CLOSE, the public open/stop APIs request OPEN/CLOSE, and
the four other pinned event origins request TOGGLE. This preserves both native
half-press producers. Requests arriving during slow Canon transitions are never
overwritten by a reset in menu_open/menu_close. Unpackaged modules directly
signaling the internal gui_sem are outside this source-patch contract.

Run the actual staged helper/consumer scheduling regressions with:

```
python3 menu_intent_test.py --ml-source /path/to/pinned/magiclantern
python3 menu_intent_test.py --ml-source /path/to/pinned/magiclantern --sanitize
```

The test extracts only the two required source files from the exact pinned Git
commit, applies the real checked builder patch, audits all eight producers and
both consumers, and compiles the patched request helper and active consumer.
It checks duplicate closes, a post delayed until another wake has closed the
menu, a newer OPEN before that delayed post, two toggles before consumption,
and new requests during slow open/close operations. It also checks that normal
task-context signaling occurs after interrupts are restored. This host
scheduling test does not prove physical-camera timing.
