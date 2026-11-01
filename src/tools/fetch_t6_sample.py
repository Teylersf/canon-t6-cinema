#!/usr/bin/env python3
"""Download the optional public-domain T6 fixture; never commit camera RAWs."""
import hashlib
from pathlib import Path
import urllib.request

URL = "https://raw.pixls.us/getfile.php/1555/nice/Canon%20-%20EOS%201300D%20-%20RAW%20(3%3A2).CR2"
EXPECTED = "e064327405b3a736b1cc89d499bc5a049cfb2800bcb06a5072e6d0a82cd296d7"
DEST = Path(__file__).resolve().parents[1] / "samples/T6_CC0.CR2"


def main():
    DEST.parent.mkdir(parents=True, exist_ok=True)
    if DEST.exists():
        if hashlib.sha256(DEST.read_bytes()).hexdigest() != EXPECTED:
            raise SystemExit("Existing fixture differs; it was left untouched.")
        print("Verified existing CC0 T6 fixture:", DEST)
        return
    tmp = DEST.with_suffix(".download")
    owned = False
    try:
        with tmp.open("xb") as out:
            owned = True
            with urllib.request.urlopen(URL, timeout=60) as src:
                h = hashlib.sha256()
                total = 0
                while block := src.read(65536):
                    total += len(block)
                    if total > 32 * 1024 * 1024:
                        raise ValueError("Fixture exceeds its download bound")
                    out.write(block)
                    h.update(block)
        if h.hexdigest() != EXPECTED:
            raise ValueError("Fixture checksum does not match")
        DEST.hardlink_to(tmp)  # atomic; refuses an existing destination
    finally:
        if owned:
            tmp.unlink(missing_ok=True)
    print("Downloaded verified CC0 T6 fixture:", DEST)
    print("Source: raw.pixls.us entry 1555; CC0 1.0 public-domain dedication")


if __name__ == "__main__":
    main()
