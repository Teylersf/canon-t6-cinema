#!/usr/bin/env python3
"""Independent FFmpeg decode and pixel recovery, never camera access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import numpy as np


def verify(binary, directory, width, height, frames, zeros=False, raw_binary=None):
    target=directory/f'{width}x{height}-{frames}-{int(zeros)}.hevc'
    command=[str(binary),str(width),str(height),str(frames),str(target)]
    if zeros:
        command.append('--zeros')
    subprocess.run(command,check=True,capture_output=True,text=True,timeout=120)
    probe=subprocess.run(['ffprobe','-v','error','-count_frames','-select_streams','v:0',
        '-show_entries','stream=codec_name,width,height,pix_fmt,nb_read_frames','-of','json',str(target)],
        capture_output=True,text=True,check=True,timeout=120)
    stream=json.loads(probe.stdout)['streams'][0]
    assert (stream['codec_name'],stream['width'],stream['height'],int(stream['nb_read_frames']))==('hevc',width,height,frames)
    decoded=subprocess.run(['ffmpeg','-nostdin','-v','error','-xerror','-err_detect','explode',
        '-i',str(target),'-f','rawvideo','-pix_fmt','yuv420p','-'],capture_output=True,check=True,timeout=120)
    assert not decoded.stderr,decoded.stderr
    assert len(decoded.stdout)==width*height*3//2*frames
    expected=hashlib.sha256()
    for frame in range(frames):
        for plane in range(3):
            pw=width if plane==0 else width//2
            ph=height if plane==0 else height//2
            x=np.arange(pw,dtype=np.uint32)
            for y in range(ph):
                if zeros:
                    row=bytes(pw)
                else:
                    values=(x*3+y*5+frame*29 if plane==0 else
                        64+x*7+y*3+frame*11 if plane==1 else
                        192+x*2+y*9+frame*13)
                    row=values.astype(np.uint8).tobytes()
                expected.update(row)
    assert hashlib.sha256(decoded.stdout).digest()==expected.digest()
    if raw_binary:
        raw=directory/(target.stem+'.yuv')
        raw.write_bytes(decoded.stdout)
        raw_target=directory/(target.stem+'-raw.hevc')
        subprocess.run([str(raw_binary),str(width),str(height),str(frames),str(raw),str(raw_target)],
            check=True,capture_output=True,timeout=120)
        recovered=subprocess.run(['ffmpeg','-nostdin','-v','error','-xerror','-err_detect','explode',
            '-i',str(raw_target),'-f','rawvideo','-pix_fmt','yuv420p','-'],capture_output=True,check=True,timeout=120)
        assert not recovered.stderr and recovered.stdout==decoded.stdout
        # Existing output and input-as-output are refused without modifying
        # either file. Short inputs fail before creating the destination.
        before=hashlib.sha256(raw_target.read_bytes()).digest()
        duplicate=subprocess.run([str(raw_binary),str(width),str(height),str(frames),str(raw),str(raw_target)],capture_output=True)
        assert duplicate.returncode and hashlib.sha256(raw_target.read_bytes()).digest()==before
        same=subprocess.run([str(raw_binary),str(width),str(height),str(frames),str(raw),str(raw)],capture_output=True)
        assert same.returncode and hashlib.sha256(raw.read_bytes()).digest()==expected.digest()
        short=directory/(target.stem+'-short.yuv')
        short.write_bytes(b'bad')
        missing=directory/(target.stem+'-must-not-exist.hevc')
        bad=subprocess.run([str(raw_binary),str(width),str(height),str(frames),str(short),str(missing)],capture_output=True)
        assert bad.returncode and not missing.exists()
        colored=directory/(target.stem+'-bt601.hevc')
        subprocess.run([str(raw_binary),str(width),str(height),str(frames),str(raw),str(colored),
            '--bt601-limited-centered'],capture_output=True,check=True,timeout=120)
        color_probe=subprocess.run(['ffprobe','-v','error','-select_streams','v:0','-show_entries',
            'stream=color_range,color_space,color_primaries,color_transfer,chroma_location','-of','json',str(colored)],
            capture_output=True,text=True,check=True,timeout=120)
        color=json.loads(color_probe.stdout)['streams'][0]
        assert color['color_range']=='tv' and color['color_space']=='smpte170m' and color['chroma_location']=='center',color
        assert color.get('color_primaries','unknown')=='unknown' and color.get('color_transfer','unknown')=='unknown',color
        color_pixels=subprocess.run(['ffmpeg','-nostdin','-v','error','-xerror','-err_detect','explode',
            '-i',str(colored),'-f','rawvideo','-pix_fmt','yuv420p','-'],capture_output=True,check=True,timeout=120)
        assert not color_pixels.stderr and color_pixels.stdout==decoded.stdout
        # Keep the FIFO on a native Unix filesystem, even when --keep is a
        # Windows-mounted path that cannot represent special files.
        with tempfile.TemporaryDirectory(prefix='t6-hevc-fifo-') as fifo_dir:
            fifo=Path(fifo_dir)/'input-fifo'
            os.mkfifo(fifo)
            fifo_target=Path(fifo_dir)/'must-not-exist.hevc'
            fifo_result=subprocess.run([str(raw_binary),str(width),str(height),str(frames),str(fifo),str(fifo_target)],
                capture_output=True,timeout=2)
            assert fifo_result.returncode and not fifo_target.exists()
    result={'width':width,'height':height,'frames':frames,'zero_heavy':zeros,
        'hevc_bytes':target.stat().st_size,'decoded_bytes':len(decoded.stdout),
        'exact_pixel_recovery':True,'raw_adapter_exact_recovery':bool(raw_binary),
        'raw_output_preservation':bool(raw_binary),'optional_bt601_centered_vui':bool(raw_binary),
        'fifo_rejected_without_blocking':bool(raw_binary),'ffmpeg_errors':0,'synthetic_not_camera':True}
    print(json.dumps(result),flush=True)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--large',action='store_true')
    parser.add_argument('--binary',type=Path)
    parser.add_argument('--raw-binary',type=Path)
    parser.add_argument('--keep',type=Path,help='new directory for retained synthetic results')
    args=parser.parse_args()
    here=Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='t6-hevc-decode-') as temporary:
        temporary=Path(temporary)
        binary=args.binary.resolve(strict=True) if args.binary else temporary/'synthetic'
        if not args.binary:
            subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-pedantic',
                str(here/'synthetic.c'),str(here/'hevc_intra.c'),'-o',str(binary)],check=True)
        raw_binary=args.raw_binary.resolve(strict=True) if args.raw_binary else temporary/'raw420'
        if not args.raw_binary:
            subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-pedantic',
                str(here/'raw420.c'),str(here/'hevc_intra.c'),'-o',str(raw_binary)],check=True)
        directory=temporary if not args.keep else args.keep.resolve()
        if args.keep:
            directory.mkdir(exist_ok=False,parents=True)
        sizes=[(2,2,2,False),(32,32,1,False),(34,66,3,False),(64,64,3,False),(96,64,2,True)]
        if args.large:
            sizes.extend([(3840,2160,2,False),(7680,4320,1,False)])
        results=[verify(binary,directory,*case,raw_binary=raw_binary) for case in sizes]
        if args.keep:
            (directory/'verification.json').write_text(json.dumps(results,indent=2)+'\n')


if __name__=='__main__':
    main()
