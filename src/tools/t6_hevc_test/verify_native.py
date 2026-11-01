#!/usr/bin/env python3
"""Decode the actual mocked-native writer's HVC and check every sample."""
import json
import csv
import zlib
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
if len(sys.argv)>2:
    report=Path(sys.argv[2]).read_text()
    rows=list(csv.reader(report.splitlines()))
    assert all(len(row)==2 for row in rows) and len({row[0] for row in rows})==len(rows)
    metadata=dict(rows)
    assert metadata['type']=='synthetic_hevc_proof' and metadata['version']=='2'
    assert metadata['optimization']=='native_Os_core_O2'
    assert metadata['proof_state_name']=='saved' and metadata['hvc_verified']=='1' and metadata['hvc_published']=='1'
    assert (int(metadata['width']),int(metadata['height']),int(metadata['frames_encoded']))==(320,180,2)
    assert int(metadata['written_bytes'])==path.stat().st_size==int(metadata['accepted_bytes'])==int(metadata['codec_bytes_committed'])
    assert int(metadata['crc32'])==zlib.crc32(path.read_bytes())
    assert int(metadata['fio_write_calls'])==3 and int(metadata['fio_read_calls'])==6
    assert int(metadata['fio_read_bytes'])==2*path.stat().st_size
    assert int(metadata['buffer_bytes'])==65536 and int(metadata['workspace_bytes'])==5632
    assert int(metadata['budget_ms'])==30000 and int(metadata['report_budget_ms'])==5000
    assert metadata['camera_capture']=='0' and metadata['recording_fps_measured']=='0'
print('Mocked native HVC: HEVC320x180/two distinct frames/exact sample recovery PASS')
