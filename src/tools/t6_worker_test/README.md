# Camera worker regression model

Run `make test` or `make asan` in this directory. The harness includes the actual
`1300D.110/src/film_develop.c`, links the shared image engine, and uses the public
T6 fixture with in-memory DryOS/file API replacements. It never accesses a camera.

The mock intentionally reproduces upstream `FIO_CreateFile` deleting an existing
path. Tests prohibit using that API for output. For `FIO_OpenFile` they model
exclusive creation honoring `O_WRONLY | O_CREAT | O_EXCL`, inject a competing TMP
inside the open after the last existence check, and check that its bytes survive.
They also model an API rejecting those flags and require failure without fallback.

These tests establish what the worker requests and how it handles modeled errors.
They do **not** establish that T6 firmware honors `O_EXCL`, that native rename
refuses replacement under races, or that camera capture scheduling and cache/DMA
behavior match the mocks. Those remain hardware validation requirements. The
rename mock refuses an existing destination; this is an explicit model assumption.

Other checks cover partial reads, output collisions, canceled/overdue jobs,
shutdown, capture becoming busy, write/read failures, rename failure, changing
source size, malformed input, original preservation, and handle cleanup.
