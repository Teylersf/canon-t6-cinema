# Still-derived 4K input for the experimental HEVC encoders

This new host C frontend reads a full-size JPEG directly, or locates Canon's
full-size JPEG inside a CR2. It uses the project's full-scale streaming JPEG
decoder, takes a **3840×2160 center crop**, optionally applies the shared
Strong/Blue anamorphic-inspired RGB effect, and writes one planar 8-bit 4:2:0
frame as raw YUV or Y4M. It never allocates a full image.

The hash-pinned public EOS 1300D CC0 fixture contains a 5184×3456 baseline JPEG;
the resulting 4K crop therefore contains captured image detail without
upscaling. This is a still-derived image conversion, not 4K video capture,
a new sensor mode, a measured recording rate or camera-side HEVC integration.
An 8K output option is deliberately absent: 5184-wide source data cannot supply
7680 independent captured columns. There are no camera addresses, DMA reads,
sensor writes, capture requests, FIO calls or firmware build changes here.

## Build and use

Linux or WSL with GCC, GNU make and Python 3/Pillow for tests:

```sh
make
./build/hevc-frame-source --input ../../samples/T6_CC0.CR2 \
  --output /path/to/NEW-captured-4k.y4m --effect strong
./build/hevc-frame-source --input ../../samples/T6_CC0.CR2 \
  --output /path/to/NEW-captured-4k.yuv --format raw
make test
make asan
```

Fetch the optional fixture first with `python3 src/tools/fetch_t6_sample.py`
from the repository root. Source: [raw.pixls.us entry 1555](https://raw.pixls.us/),
[CC0 public-domain dedication](https://creativecommons.org/publicdomain/zero/1.0/).
The tests require its exact SHA-256 and check it again after conversion.
Do not commit RAWs, outputs or personal images.

The only accepted output dimensions are 3840×2160. Source dimensions must be
at least that large and at most 6000×4000. Crop origins round down to an even
coordinate, keeping chroma pairs aligned; a 5184×3456 source crops at 672,648.
There is no arbitrary resizer or optical field-of-view expansion.

`--effect none` is the default; `--effect strong` uses the existing Strong/Blue
recipe after cropping. It does not add oval optical bokeh or film-stock color
grading. The effect has its own 384 KiB arena and reports actual arena usage.

## Pixel format and color contract

Both output forms contain one frame with tightly packed Y, Cb, Cr planes:
8,294,400 Y bytes, 2,073,600 Cb bytes and 2,073,600 Cr bytes; 12,441,600 total.
There are no row padding bytes. Y4M adds a single header and `FRAME` marker.
Its `F1:1` value is a presentation label for this one image, never a measured
camera frame rate. Raw input consumers must be told 3840×2160/yuv420p explicitly.

The decoder delivers nonlinear RGB8. Conversion uses these explicit BT.601
studio-range integer approximations:

```text
Y  = clip(16  + floor(( 66R +129G + 25B +128)/256),16,235)
Cb = clip(128 + floor((-38ΣR -74ΣG +112ΣB +512)/1024),16,240)
Cr = clip(128 + floor((112ΣR -94ΣG - 18ΣB +512)/1024),16,240)
```

Each chroma sum covers a 2×2 RGB group, giving centered box-filtered 4:2:0.
Signed floor division is implemented explicitly, without relying on signed
right-shift behavior. Y4M declares `C420jpeg XCOLORRANGE=LIMITED`; its custom
`XCOLORMATRIX=BT601` tag documents the matrix but readers may ignore that tag.
This code does not perform ICC conversion or reconstruct linear sensor values.
An HEVC wrapper should signal matching range/matrix/chroma metadata, or consumers
must supply it; resolution alone does not select the matrix used here.

## Bounds and file ownership

Inputs are regular files up to 64 MiB. Embedded JPEGs are limited to 12 MiB, with
a 128 KiB header and 8 KiB bounded read callbacks. Decode scratch is 768 KiB;
effect scratch is 384 KiB. A previous RGB row plus Y/Cb/Cr rows use 19,200 bytes. The
JSON result reports owned heap budget and arena peaks; libc overhead and stack
are not included. Full sensor-RAW decoding and live frame buffers are unused.

The output opens with `O_CREAT|O_EXCL`, preserving existing files, links and
the source. Plane rows are written at checked offsets in the new file, so
chroma need not be held for the entire frame. A failed job returns nonzero and
retains its newly created partial output; it must not be treated as complete.
A successful job checks row counts, source identity/length, final output length,
flush and close. The source is opened read-only. The tests independently check
source hashes, full neutral-color planes, malformed/truncated inputs, size
bounds, no-upscaling behavior, link collisions, captured-detail crop geometry,
effect changes and independent Y4M decoding when FFmpeg is installed.

GPL-2.0-only. This frontend reuses the existing engine under its project license.
