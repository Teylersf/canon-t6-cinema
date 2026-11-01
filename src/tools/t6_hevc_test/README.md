# Native HEVC job fault tests

These tests compile the actual `film_hevc.c` camera job and custom HEVC core
against mocked Canon APIs. No camera, memory card or firmware image is accessed.

```sh
make test
make asan
```

The successful path streams two distinct generated320×180 frames through the
actual codec, aggregates its4KiB callbacks into a managed64KiB DMA buffer,
creates a new TMP with literal T6 mode `0x8501`, verifies and renames it, then
verifies the published HVC again. The same buffer is reused for both readbacks.
Three physical writes include the checked final tail; six reads cover the two
verification passes. A direct unbatched codec reference is byte-identical.
An independent bitwise CRC32 checks the saved bytes and the standard
`123456789` vector. External FFmpeg independently compares every decoded sample;
Python CSV/zlib checks the persistent report's sizes, CRC and counter metadata.

Fault coverage includes:

- Every319 proof and27 report cancellation/deadline guard boundaries in the
  successful run, plus all eight guard types at key proof and report points.
- Stop, shutdown, capture, half-shutter, LV, movie, recording and deadline
  changes occurring inside the final blocking size query. Published output and
  its result path remain present and are not called verified.
- Workspace, DMA and report allocation failures, arithmetic-only DMA-class
  validation and freeing both proof allocations before the report's separate
  256-byte readback allocation. Only original returned pointers reach FIO/free.
- Short full-batch writes where actual written bytes exceed earlier accepted
  whole callbacks; exact partial CRC, checked final-tail failures and no retry.
  Cancellation with queued bytes but no physical write performs no tail flush.
- Thirty/120/300-second selected budgets and fresh5-second deadline reporting.
  A transient capture/Stop cleared during cleanup still suppresses reporting.
- Report memory/class/I/O/readback failures, occupied slots and publication
  races preserve the primary HVC outcome. Late report failure retains its CSV
  and path. A snapshot comparison protects every primary result field.
- Invalid size and preflight `CODEC_NOT_CALLED` reporting; clock wrap.
- Existing output/TMP slots, all sixteen occupied slots, a late TMP creation
  race and a late destination rename collision, preserving foreign bytes.
- Short/failed writes, short/zero/negative/oversized reads, open/size/rename
  failures, corruption and truncation before and after publication.

The virtual filesystem refuses destructive creation modes and only permits
cleanup of the TMP created by the current job. ASan/UBSan and leak detection
check the real code and mocked ownership paths. These host checks establish
neither physical FIO semantics nor camera speed; native emulation and the
physical saved-file test remain separate evidence. The native Makefile requests
`-O2` only for the HEVC core; the native job retains `-Os`. These tests validate
behavior and metadata, not physical speed or the compiler's real-time behavior.
