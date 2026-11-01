# Native HEVC job fault tests

These tests compile the actual `film_hevc.c` camera job and custom HEVC core
against mocked Canon APIs. No camera, memory card or firmware image is accessed.

```sh
make test
make asan
```

The successful path streams two distinct generated320×180 frames through the
actual codec, creates a new TMP with literal T6 mode `0x8501`, verifies and
renames it, then verifies the published HVC again. An independent bitwise CRC32
checks the saved bytes and the standard `123456789` vector. External FFmpeg
decodes the exported HVC and independently compares every visible sample.

Fault coverage includes:

- Every395 actual cancellation/deadline guard boundary in the successful run,
  plus all eight guard types at allocation/write/readback/publication points.
- Stop, shutdown, capture, half-shutter, LV, movie, recording and deadline
  changes occurring inside the final blocking size query. Published output and
  its result path remain present and are not called verified.
- Workspace/readback allocation failures, original-pointer cleanup, and freeing
  the codec workspace before allocating the FIO readback buffer.
- Invalid size and preflight `CODEC_NOT_CALLED` reporting; clock wrap.
- Existing output/TMP slots, all sixteen occupied slots, a late TMP creation
  race and a late destination rename collision, preserving foreign bytes.
- Short/failed writes, short/zero/negative/oversized reads, open/size/rename
  failures, corruption and truncation before and after publication.

The virtual filesystem refuses destructive creation modes and only permits
cleanup of the TMP created by the current job. ASan/UBSan and leak detection
check the real code and mocked ownership paths. These host checks establish
neither physical FIO semantics nor camera speed; native emulation and the
physical saved-file test remain separate evidence.
