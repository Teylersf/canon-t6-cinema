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

## Required evidence before a camera video implementation

1. Matching T6 1.1.0 ROM images and a reproducible physical-camera baseline.
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
