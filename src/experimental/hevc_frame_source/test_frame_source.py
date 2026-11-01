#!/usr/bin/env python3
"""Independent host bounds, file preservation and captured-detail 4K checks."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

EXE = Path(sys.argv[1]).resolve(strict=True)
FIXTURE = Path(sys.argv[2]).resolve(strict=True)
PIN = 'e064327405b3a736b1cc89d499bc5a049cfb2800bcb06a5072e6d0a82cd296d7'
Y_SIZE = 3840 * 2160
C_SIZE = Y_SIZE // 4
RAW_SIZE = Y_SIZE + 2 * C_SIZE


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def call(source, output, *options, good=True):
    run = subprocess.run([str(EXE), '--input', str(source), '--output', str(output), *options],
                         capture_output=True, text=True, timeout=120)
    if good:
        assert run.returncode == 0, run.stderr
        report = json.loads(run.stdout)
        assert (report['width'], report['height'], report['frames']) == (3840, 2160, 1)
        assert report['plane_bytes'] == RAW_SIZE and report['live_capture'] is False
        assert report['still_derived'] is True and report['matrix'] == 'BT601'
        assert report['range'] == 'limited' and report['owned_heap_budget'] < 1400000
        assert report['decode_arena_peak'] <= 768 * 1024
        assert report['effect_arena_peak'] <= 384 * 1024
        assert output.stat().st_size == report['file_bytes']
        return report
    assert run.returncode != 0, (source, output, options)
    return run


def y4m_data(path):
    with path.open('rb') as source:
        header = source.readline()
        assert header.startswith(b'YUV4MPEG2 W3840 H2160 F1:1 Ip A1:1 C420jpeg ')
        assert b'XCOLORRANGE=LIMITED' in header and b'XCOLORMATRIX=BT601' in header
        assert source.readline() == b'FRAME\n'
        data = source.read()
    assert len(data) == RAW_SIZE
    return data


def change_sof(data, width, height):
    content = bytearray(data)
    position = content.index(b'\xff\xc0')
    content[position + 5:position + 7] = height.to_bytes(2, 'big')
    content[position + 7:position + 9] = width.to_bytes(2, 'big')
    return content


def main():
    assert digest(FIXTURE) == PIN, 'Public CC0 fixture does not match its hash pin'
    with tempfile.TemporaryDirectory(prefix='t6-hevc-source-test-') as directory:
        root = Path(directory)
        gray = root / 'gray.jpg'
        Image.new('RGB', (3840, 2160), (128, 128, 128)).save(gray, quality=95, subsampling=0)
        gray_before = digest(gray)
        flat = root / 'gray.y4m'
        result = call(gray, flat)
        assert result['crop_left'] == result['crop_top'] == 0
        data = y4m_data(flat)
        # Known independent neutral studio-range vectors: RGB128 maps to Y126,
        # neutral chroma128. Every sample is checked, including final rows.
        assert data[:Y_SIZE] == bytes([126]) * Y_SIZE
        assert data[Y_SIZE:] == bytes([128]) * (2 * C_SIZE)
        assert digest(gray) == gray_before
        bars = root / 'color-bars.jpg'
        colors = [(0,0,0), (255,255,255), (255,0,0), (0,255,0),
                  (0,0,255), (255,255,0), (0,255,255), (255,0,255)]
        image = Image.new('RGB', (3840,2160))
        draw = ImageDraw.Draw(image)
        for index, color in enumerate(colors):
            draw.rectangle((index*480,0,(index+1)*480-1,2159),fill=color)
        image.save(bars,quality=95,subsampling=0)
        bar_output = root / 'color-bars.yuv'
        call(bars,bar_output,'--format','raw')
        bar_data = bar_output.read_bytes()
        decoded_bars = Image.open(bars).convert('RGB')
        for row in (0,1080,2159):
            for index in range(8):
                column = index*480+240
                red, green, blue = decoded_bars.getpixel((column,row))
                # Independent floating BT.601 equations; the C frontend uses
                # bounded integer approximations, so allow two code values.
                luma = 0.299*red + 0.587*green + 0.114*blue
                expected = (16+219*luma/255,
                    128+224*(blue-luma)/(255*2*(1-0.114)),
                    128+224*(red-luma)/(255*2*(1-0.299)))
                chroma_offset = (row//2)*1920 + column//2
                actual = (bar_data[row*3840+column], bar_data[Y_SIZE+chroma_offset],
                          bar_data[Y_SIZE+C_SIZE+chroma_offset])
                assert all(abs(a-round(e))<=2 for a,e in zip(actual,expected)), (actual,expected)
        keep = root / 'keep.y4m'
        keep.write_bytes(b'KEEP EXISTING FILE')
        call(gray, keep, good=False)
        assert keep.read_bytes() == b'KEEP EXISTING FILE'
        call(gray, gray, good=False)
        assert digest(gray) == gray_before
        hardlink = root / 'hard.jpg'
        hardlink.hardlink_to(gray)
        call(gray, hardlink, good=False)
        symlink = root / 'linked.y4m'
        symlink.symlink_to(keep)
        call(gray, symlink, good=False)
        assert keep.read_bytes() == b'KEEP EXISTING FILE' and digest(gray) == gray_before

        bad = root / 'bad.jpg'
        bad.write_bytes(b'not a supported image')
        small = root / 'small.jpg'
        Image.new('RGB', (1920, 1080)).save(small)
        excessive = root / 'excessive.jpg'
        excessive.write_bytes(change_sof(gray.read_bytes(), 7680, 4320))
        progressive = root / 'progressive.jpg'
        Image.new('RGB', (3840, 2160)).save(progressive, progressive=True)
        huge = root / 'huge.CR2'
        with huge.open('wb') as target:
            target.truncate(64 * 1024 * 1024 + 1)
        pipe = root / 'input-pipe'
        os.mkfifo(pipe)
        for index, source in enumerate([bad, small, excessive, progressive, huge, pipe, root]):
            output = root / f'rejected{index}.y4m'
            call(source, output, good=False)
            assert not output.exists(), source
        for options in [('--format', 'invalid'), ('--effect', 'invalid'), ('--width', '7680')]:
            output = root / 'bad-option.y4m'
            call(gray, output, *options, good=False)
            assert not output.exists()
        truncated = root / 'truncated.jpg'
        truncated.write_bytes(gray.read_bytes()[:len(gray.read_bytes()) // 2])
        truncated_before = digest(truncated)
        failed_output = root / 'partial.y4m'
        call(truncated, failed_output, good=False)
        assert digest(truncated) == truncated_before

        reference = root / 'captured-4k.yuv'
        strong = root / 'captured-4k-strong.y4m'
        reference_report = call(FIXTURE, reference, '--format', 'raw')
        strong_report = call(FIXTURE, strong, '--effect', 'strong')
        for report in [reference_report, strong_report]:
            assert (report['source_width'], report['source_height']) == (5184, 3456)
            assert (report['crop_left'], report['crop_top']) == (672, 648)
        plain = reference.read_bytes()
        effected = y4m_data(strong)
        assert len(plain) == RAW_SIZE and plain != effected
        for plane in [plain, effected]:
            assert min(plane[:Y_SIZE]) >= 16 and max(plane[:Y_SIZE]) <= 235
            assert min(plane[Y_SIZE:]) >= 16 and max(plane[Y_SIZE:]) <= 240
        ffmpeg = shutil.which('ffmpeg')
        if ffmpeg:
            subprocess.run([ffmpeg, '-v', 'error', '-xerror', '-nostdin', '-i', str(strong),
                            '-f', 'null', '-'], check=True, timeout=120)
        assert digest(FIXTURE) == PIN
        print(json.dumps({'result':'passed','source':'hash-pinned CC0 Canon EOS1300D CR2',
                          'captured_source_crop':reference_report,'strong_blue_crop':strong_report,
                          'independent_y4m_decode':bool(ffmpeg),'source_unchanged':True,
                          'camera_operation_tested':False}, indent=2))


if __name__ == '__main__':
    main()
