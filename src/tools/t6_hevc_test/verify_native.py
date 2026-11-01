#!/usr/bin/env python3
"""Decode the actual mocked-native writer's HVC and check every sample."""
import json
from pathlib import Path
import subprocess
import sys
import numpy as np

path=Path(sys.argv[1]).resolve(strict=True)
probe=subprocess.run(['ffprobe','-v','error','-count_frames','-select_streams','v:0',
    '-show_entries','stream=codec_name,width,height,nb_read_frames','-of','json',str(path)],
    check=True,capture_output=True,text=True,timeout=60)
stream=json.loads(probe.stdout)['streams'][0]
assert (stream['codec_name'],stream['width'],stream['height'],int(stream['nb_read_frames']))==('hevc',320,180,2)
decoded=subprocess.run(['ffmpeg','-nostdin','-v','error','-xerror','-err_detect','explode',
    '-i',str(path),'-f','rawvideo','-pix_fmt','yuv420p','-'],check=True,capture_output=True,timeout=60)
assert not decoded.stderr
expected=[]
for frame in range(2):
    for plane in range(3):
        width,height=(320,180) if plane==0 else (160,90)
        y,x=np.indices((height,width),dtype=np.uint32)
        values=(3*x+5*y+29*frame if plane==0 else 64+7*x+3*y+11*frame if plane==1 else 192+2*x+9*y+13*frame)
        expected.append(values.astype(np.uint8).tobytes())
assert decoded.stdout==b''.join(expected)
print('Mocked native HVC: HEVC320x180/two distinct frames/exact sample recovery PASS')
