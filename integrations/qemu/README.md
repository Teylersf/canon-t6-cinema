# T6 emulator validation

QEMU source: [reticulatedpines/qemu-eos](https://github.com/reticulatedpines/qemu-eos/tree/4b667a1d3c08ab7a55835d15ddbd884fa754946d),
commit `4b667a1d3c08ab7a55835d15ddbd884fa754946d`, branch `qemu-eos-v4.2.1`.
The required keycodemapdb submodule was checked out at
`6b3d716e2b6472eb7189d3220552280ef3d832ce` through its upstream repository.

The private T6 ROM1 matched the dumper's MD5. Original ROMs and logs are kept
outside Git. Neither the emulation tests nor this patch access the USB camera
or physical SD card.

## Explicit diagnostic limitations

The original ROM1's final 64 KiB reset bank was blank. A separate emulator copy
copies bytes `[0x10000:0x20000]` to `[0x1ff0000:0x2000000]`, following the
[historical developer workaround](https://www.magiclantern.fm/forum/index.php?topic=16534.msg210728#msg210728).
The size remains 32 MiB; every byte before the final 64 KiB is unchanged. The
original dump is preserved. This is reconstruction for emulation, not a camera
firmware patch.

ROM0's checksum does not match and its entire 32 MiB is one repeated four-byte
word. The stock emulator rejects this input. The included
`1300D-unverified-rom0.patch` allows that exact diagnostic shape only for model
1300D and filename ROM0.BIN, with an explicit warning. It retains the recorded
bytes and address map. All other inputs keep the original mirroring check.
This does not establish whether the physical bank is absent or the dump failed.

These two accommodations must accompany any report of an emulator pass.
Successful GUI or filesystem tests do not validate physical DMA, cache timing,
encoder throughput, power loss, card controllers, real-time effects or FPS.

## Build used

Build on Linux/WSL with LF source files and an out-of-tree build directory.
Dependencies used were GCC/make, Python 3, pkg-config, GLib, pixman, zlib,
libfdt, libpng and libjpeg development packages. The official repository's
[build instructions](https://github.com/reticulatedpines/qemu-eos/blob/4b667a1d3c08ab7a55835d15ddbd884fa754946d/magiclantern/README.md)
are the owning reference.

```sh
../qemu-eos/configure --target-list=arm-softmmu --enable-plugins \
  --disable-docs --enable-vnc --disable-gtk --disable-sdl --disable-vte \
  --disable-capstone --disable-slirp --disable-werror --python=/usr/bin/python3
make -j8
make plugins -j8
```

Apply the diagnostic patch only to this isolated emulator source, after checking
it against the pinned revision. It is never included in AUTOEXEC.BIN.

## Private runtime layout

```text
private-rom-root/1300D/110/ROM0.BIN
private-rom-root/1300D/110/ROM1.BIN
```

Set `QEMU_EOS_WORKDIR` to `private-rom-root` and select
`-M '1300D,firmware=110;boot=0'` for Canon startup, or `boot=1` to test ML.
This boot flag affects emulator memory only. Supply both an SD and a dummy CF
image, as required by the current model. Initial tests used `-snapshot`.
Filesystem-output tests used writable copies of fresh, private virtual disks.
Display was headless; QMP used local Unix sockets, with networking disabled.

Virtual cards were derived from the pinned repository's `sd.qcow2.xz` and
populated with our build. The still-processing fixture is the hash-pinned public
CC0 T6 sample. Captures and logs from user-owned cameras must remain private.

The emulator's JPCORE supplies synthetic completions. No emulator wall-clock
result is reported as the camera's actual processing speed or capture rate.
