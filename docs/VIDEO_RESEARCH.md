# T6 in-camera video effects: evidence and missing validation

Research target: EOS Rebel T6 / EOS 1300D / Kiss X80, Canon firmware 1.1.0.
Magic Lantern source pinned at `142472e95054d73640f50789ac0fd66325499d82`.
Assessment date: 2026-10-31, America/Los_Angeles.

The experimental T6 Cinema artifact develops still-photo JPEG previews only.
It does not apply advanced effects to a saved movie. Its worker waits in movie
mode and does not add movie-encoder hooks or sensor-mode changes. Successful
ARM compilation and desktop fault tests do not establish physical-camera boot
or movie-recording reliability.

## The existing display interface is not an encoder interface

[ML display-filter callbacks](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/module.h#L39)
operate on Live View source/destination buffers.
[The implementation](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/tweaks.c#L3197)
processes a separate LCD image and redirects the displayed buffer to it, using
a 720×480 output allocation. This does not establish that Canon's H.264 encoder
consumes that processed image. An attractive preview is not evidence of an
effect in the saved MOV.

The T6 [LV buffer constants](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/platform/1300D.110/consts.h#L58)
carry an explicit warning that they were copied, probably from the 600D, and
need reconfirmation. The same file's movie structures are described as
untested; buffer usage is a hardcoded placeholder. The T6
[movie-configuration structure](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/platform/1300D.110/include/platform/mvr.h#L6)
also warns that many values describe the 550D. These comments preclude treating
those fields as a validated video-processing API.

## QEMU helps with reverse engineering, but does not prove recorded effects

The maintained QEMU branch has an explicit
[1300D model](https://github.com/reticulatedpines/qemu-eos/blob/qemu-eos-v4.2.1/hw/eos/model_list.c#L227)
and [machine registration](https://github.com/reticulatedpines/qemu-eos/blob/qemu-eos-v4.2.1/hw/eos/eos.c#L140).
The upstream T6 commit reports QEMU operation. Saying the 1300D is entirely
unsupported in QEMU would be incorrect.

The emulator loads matching ROM images. Its
[JPCORE implementation](https://github.com/reticulatedpines/qemu-eos/blob/qemu-eos-v4.2.1/hw/eos/engine.c#L1090)
supplies status responses and synthetic completion interrupts. That is not
evidence of faithful real-time H.264 output or physical sensor/encoder timing.
A passing emulator boot cannot validate visual effects in an actual saved MOV.

## Local ROM inspection after acquisition

A private ROM1 dump now matches the checksum recorded by the camera-side
dumper. Read-only ARM analysis corroborates the movie-recorder allocation,
frame counter, byte counter, H.264 request path, and distinct recording-mode
YUV path functions. This is stronger evidence than finding nonblank words at
upstream stub addresses, but it does not establish a callable public ABI.

The encoder request saves an input address and later programs a DMA read from
it. Its read-completion callback runs after the read; it is not an established
place to perform pre-encode effects. The mode-specific YUV parameter functions
also do not by themselves prove active buffer addresses, stride, channel order,
producer/consumer ownership, reuse timing, or cache coherency. These remain
required observations before inserting image processing.

ROM0 fails its camera-recorded checksum and consists entirely of one repeated
four-byte pattern. This cannot distinguish an absent or inaccessible bank from
an unsuccessful dump. The original files remain unchanged. Any emulator-only
boot-sector reconstruction or narrowly scoped loader exception must be recorded
as a limitation of that emulator run; it does not validate ROM0 or the camera's
physical memory map. Raw firmware, disassembly, private identifiers and analysis
logs remain in the ignored local `rom/` directory.

## Next bounded encoder-boundary probe

Further static inspection identifies a stronger candidate chain than the LCD
display filter. The 720p YUV path programs an EDMAC write into a supplied
address and registers its own YUV-write completion callback. Canon's EVF frame
handling also calls an encode controller that computes an input address using
dynamically queried configuration, then posts it to the recorder's input queue.
The recorder forwards that input through the H.264 request path to EDMAC read
setup and start. These are verified code paths; their execution order and
buffer identity during a recording have not yet been observed here.

The next probe is an emulator-only, bounded execution-breakpoint trace:

1. Start with the normal Canon 720p recording mode. Record the producer's write
   destination and existing write-completion callback, the encode controller's
   submitted address, and the recorder's queued and handled input addresses.
2. Record the H.264 request address, input DMA setup/start and existing read
   completion, together with frame counters and a sequence number. Initially
   collect registers and checked RAM metadata only, with no pixel reads.
3. Match the same buffer through that chain and its next producer write. Check
   for overlap, reordering and reuse. Do not treat a recorder event-flag
   operation as proof that image-buffer ownership was released.
4. Use hardware execution breakpoints without firmware patches, inferior
   function calls, callback replacement, register writes or MMIO reads. Keep
   records bounded and private. The local observer checks the selected
   instructions against its ROM manifest.
5. If the chain executes under QEMU's synthetic completions, use that result
   only to validate the call graph and observation fields. Physical-camera
   ownership, stride, pixel format, cache handling and available processing time
   still require measurements during an unchanged baseline recording.

In particular, registering a replacement EDMAC callback would change Canon's
pipeline and is not this passive probe. No pre-encode effects hook is enabled.

The separate [packed-YUV prototype](../src/experimental/yuv_anamorphic/README.md)
provides bounded integer horizontal highlight streaks without a JPEG stage or
RGB conversion. It accepts explicit generic packed layouts, ranges and chroma
representations; none is asserted to match this T6 encoder. Host/ARM tests do
not establish physical processing time. It is excluded from the current camera
build and requires verified exclusive buffer ownership and cache handling
before integration.

A separate stock-Canon emulator run armed these observation points without
changing firmware or target registers. The emulator's documented `V` control
reached Canon movie Live View. During that transition Canon logged an image
power failure and a shutdown request. The subsequent `L` recording-button event
was delivered, but no selected producer or encoder point executed: the bounded
trace contained zero frame-chain records. The debugger detached and that
isolated VM was closed normally. This identifies a movie-startup blocker in
this emulator setup; it does not demonstrate a physical-camera power fault or
a usable encoder boundary. The ROM reconstruction, unvalidated ROM0 and
synthetic-completion limitations above still apply. The next useful evidence
is a successful physical-camera boot and unchanged baseline MOV recording.

## Required evidence before a camera video implementation

1. A reproducible physical-camera baseline and resolution of relevant ROM
   acquisition gaps. ROM1 is available locally; ROM0 is not validated.
2. Verified encoder input buffers: addresses, dimensions, stride, pixel format,
   queue ownership and lifetime, cache coherency, and frame-boundary timing.
3. A minimal pre-encode experiment whose effect is visible in the saved MOV,
   with original recording controls, audio and frame pacing preserved.
4. Measured ARM processing time and memory traffic at each candidate resolution
   and frame rate. 1080p30 alone is 62.2 million pixels per second; compiling a
   still-image engine supplies no camera throughput measurement.
5. Repeatable cancellation, camera-off behavior, recording restart, temperature
   and sustained-recording checks. Never infer safety from another model's ROM
   addresses or merely enable untested platform definitions.

No such saved-video validation or experimental frame-rate ceiling has been
measured for this build. This is a missing hardware/reverse-engineering result,
not a proof that every possible implementation is impossible.

## Established, narrower in-camera option

Canon supports registering three user-defined Picture Styles on this camera
using EOS Utility. This can provide camera-rendered color and contrast styling.
It cannot express our spatial highlight streaks, bloom or optical anamorphic
bokeh. It is a narrower alternative, not fulfillment of advanced effects baked
into video. See [Canon EOS Utility instructions](https://cam.start.canon/en/S003/manual/html/UG-02_CameraSetting_0020.html)
and the [T6/1300D manual](https://gdlp01.c-wss.com/gds/2/0300023332/01/EOS_1300D_Instruction_Manual_EN.pdf).

See [HIGH_FPS.md](HIGH_FPS.md) for recording-rate evidence and the measurement
contract. These research notes do not enable an experimental video mode.
