# T6 lower-resolution and high-frame-rate investigation

Target: EOS Rebel T6 / EOS 1300D / Kiss X80, Canon firmware 1.1.0.
Source audited: Magic Lantern `142472e95054d73640f50789ac0fd66325499d82`.
Assessment date: 2026-11-01, America/Los_Angeles.

## What is established

Canon's published NTSC recording modes are:

| Saved resolution | Nominal rate | Actual nominal timebase |
| --- | --- | --- |
| 1920×1080 | 30 or 24 fps | 29.97 or 23.98 fps |
| 1280×720 | 60 fps | 59.94 fps |
| 640×480 | 30 fps | 29.97 fps |

Source: [Canon T6 specifications, Video Shooting](https://www.usa.canon.com/support/p/eos-rebel-t6-1).
These are manufacturer specifications, not measurements of this experiment.
Lowering the selected output resolution to VGA does not itself unlock a higher
published frame rate. PAL settings have different available rates; log the
camera's selected video system during testing.

The owner has since supplied two normal Canon MOVs recorded with the r12
diagnostic card: 349 decoded frames at 1920×1080, 30000/1001 fps over 11.645 s;
and 800 decoded frames at 1280×720, 60000/1001 fps over 13.347 s. Both video and
audio decoded without errors, with constant expected video PTS spacing. These
short saved-video baselines do not establish calibrated unique sensor exposures
or a higher capture rate. See the [physical results](HARDWARE_BASELINE.md).

**No experimental T6 recording rate above these modes has been verified here.**
No reliable 90/120/240-fps capability or maximum overclock rate can be quoted
from the source currently available.

## What the source contains, and what it does not establish

The T6 has [register-read helpers](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/platform/1300D.110/fps-engio_per_cam.c)
and [timer-register definitions](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/platform/1300D.110/fps-engio_per_cam.h).
The [T6-specific branch](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/fps-engio.c#L399)
defines a timing-generator base and mode-dependent timer minima. This branch
does not include measurement logs establishing a stable recorded-video ceiling.
Do not describe these specific timer constants as explicitly copied unless
further provenance establishes that; unlike the LV buffer definitions, the
timer branch itself does not contain that warning.

The generic code explains that its timing-generator base should be derived
from observed timers and known frame rates across modes. Its generic menu
contains requests up to 70 fps, but selectable requested values are not measured
T6 output rates. Sensor readout, encoder acceptance, recording throughput,
preview updates, and the MOV playback timebase are separate quantities.

Crucially, the pinned T6 [feature configuration](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/platform/1300D.110/features.h)
does not enable `FEATURE_FPS_OVERRIDE`. The
[override gate](https://github.com/reticulatedpines/magiclantern_simplified/blob/142472e95054d73640f50789ac0fd66325499d82/src/fps-engio.c#L161)
returns disabled when that feature is absent. The T6 Cinema still-image overlay
does not change that configuration or introduce sensor-timer writes.

Arithmetic using unvalidated timer minima is not a recording benchmark. A
formula for requested sensor timing does not establish which frames the encoder
actually saves. Calibrate the constants and observe the complete recording path
before using them as evidence of a ceiling or a basis for experimental writes.

Private, read-only ROM analysis now identifies the H.264 request chain and
separate 720p, Full HD and VGA YUV paths. A comparison against a numeric frame
rate in shared firmware code is not evidence that this camera accepts that
rate: some such branches lead to assertions. No new recording mode or measured
FPS ceiling follows from these static findings. No sensor registers were
written and no FPS override was enabled during this inspection.

## Measurement contract

The objective is **independently captured, uniquely timed frames saved to a
valid movie**, not a higher FPS label, duplicated frames, a faster LCD refresh,
or a playback-speed change.

1. Identify the physical camera and firmware; preserve a working Canon/ML
   baseline, record lens, card, power, video system, resolution, exposure and
   temperature. Begin with untouched Canon 720p59.94 and VGA29.97 recordings.
2. Obtain matching ROMs and read-only timing/encoder traces in each normal mode.
   Verify the relevant timer registers and input-buffer lifecycle. Merely
   connecting USB does not provide these observations.
3. Use a calibrated visual time reference and non-repeating motion that can
   distinguish exposures at the candidate capture rate. A phone/monitor that
   updates at only 60 Hz cannot by itself prove 120 independent frames/second.
4. Decode the saved MOV. Record frame count, PTS/DTS, timebase, playback rate,
   real capture-time span from the visual reference, duplicates/drops, geometry,
   readout artifacts and recording failures. Count unique exposures against
   **capture time**, because intentionally slowed playback changes movie duration.
   Header metadata or frame hashes alone cannot establish independent captures.
5. Benchmark the effects pipeline independently, then combine it with capture
   only after a verified pre-encode hook exists. Measure frame-time percentiles,
   backlog, dropped frames, audio drift, sustained duration and temperature.
6. Accept a candidate only after repeatable saved-video results and normal
   stop/restart/power-off behavior. Retain the baseline recording and every
   experimental clip with the exact build hash and settings. The highest rate
   that passes these checks is an observed result for that mode/configuration,
   not a universal hardware ceiling.

No speculative timer writes, FPS-override enablement or camera installation
were performed for this document. The current artifact's processing is still
limited to photographs. See [VIDEO_RESEARCH.md](VIDEO_RESEARCH.md) for the
separate encoder-hook and baked-effects blockers.
