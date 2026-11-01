#!/usr/bin/env python3
"""Check the actual builder's reserved-RAM rejection before producing firmware."""
import importlib.util
from pathlib import Path
import struct
import tempfile

spec = importlib.util.spec_from_file_location("builder", Path(__file__).parents[1] / "build-t6.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

def image(memory=0x70000, address=builder.ML_RAM_START, files=4, machine=40,
          entry=builder.ML_RAM_START, physical=None, phnum=1):
    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, machine, 1, entry, 52, 0, 0, 52, 32, phnum, 0, 0, 0)
    segment = struct.pack("<8I", 1, 84, address, address if physical is None else physical, files, memory, 7, 0x100)
    return header + segment + b"test"

def main():
    count = 0
    with tempfile.TemporaryDirectory(prefix="t6-ram-guard-") as name:
        elf = Path(name) / "test.elf"
        for memory in (1, 0x70000, 0x80000):
            # One byte cannot hold a four-byte payload; keep that case valid.
            elf.write_bytes(image(memory=memory, files=min(memory, 4)))
            report = builder.validate_ram_image(elf)
            assert report["static_load_bytes"] == memory
            assert report["remaining_reserved_bytes"] == 0x80000 - memory
            count += 1
        invalid = [b"", b"\0" * 100, image(memory=0), image(memory=0x80001),
                   image(address=builder.ML_RAM_START-4), image(physical=builder.ML_RAM_START+4),
                   image(machine=62), image(entry=builder.ML_RAM_START+4), image(files=5),
                   image(memory=3), image(phnum=0), image(phnum=17), image(phnum=2),
                   image(memory=0xffffffff)]
        for data in invalid:
            elf.write_bytes(data)
            try:
                builder.validate_ram_image(elf)
            except SystemExit:
                count += 1
            else:
                raise AssertionError("Unsafe ELF was accepted")
    print("T6 ELF static-RAM guard:", count, "checks passed")

if __name__ == "__main__":
    main()
