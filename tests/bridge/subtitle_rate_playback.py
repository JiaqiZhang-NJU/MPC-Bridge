"""Sample uninterrupted embedded cues while the actual Bilibili hold runs."""
import argparse
import ctypes
import io
import json
import os
import pathlib
import shutil
import socket
import subprocess
import time
from PIL import Image
from embedded_subtitle_playback import mkv, select
from dual_subtitle_playback import ROOT, dialog, click, seek, request
from quality_cache_playback import user, window, wait, state
from real_subtitle_playback import control_text

def run(args):
    folder=ROOT/'_bin/bridge/subtitle-rate-qa'/f'vr{args.renderer}-{args.kind}-{time.time_ns()}'
    folder.mkdir(parents=True)
    # Alternate recognized extension exercises connected streams without the
    # local-MKV full-track worker; the container itself is still Matroska.
    movie=folder/('continuous.webm' if args.live else 'continuous.mkv')
    mkv(movie,ass=args.kind=='ass',pgs=args.kind=='pgs',continuous=not args.boundaries)
    player=folder/'rate-player.exe'
    shutil.copy2(args.player or ROOT/'_bin/mpc-be_x64/mpc-be64.exe',player)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    queue='SubpicCount=0\n' if args.unbuffered else ''
    player.with_suffix('.ini').write_text(f'[MPC-Bridge]\nShortcutProfile=bilibili\n[Audio]\nMute=1\n[Video]\nVideoRenderer={args.renderer}\n{queue}[Settings]\nSnapShotSubtitles=1\n[WebServer]\nEnableWebServer=1\nEnablePreview=1\nLocalhostOnly=1\nPort={port}\n',encoding='utf-16')
    environment=os.environ.copy()
    if args.presenter_log:
        args.presenter_log=args.presenter_log.resolve()
        args.presenter_log.write_text('')
        environment['MPC_SUBTITLE_FRAME_PROBE']=str(args.presenter_log)
    process=subprocess.Popen([str(player),str(movie),'/new','/minimized'],env=environment)
    hwnd=None; samples=[]
    try:
        wait(port,lambda s:int(s.get('position',0))>500)
        hwnd=window(process.pid);assert hwnd
        user.ShowWindow(ctypes.c_void_p(hwnd),9)
        user.SetWindowPos(ctypes.c_void_p(hwnd),ctypes.c_void_p(-1),100,100,1000,700,0x0040)
        user.SetForegroundWindow(ctypes.c_void_p(hwnd));time.sleep(.5)
        user.PostMessageW(hwnd,0x111,888,0)
        wait(port,lambda s:s['state']=='1')
        dlg=dialog(hwnd);select(dlg,0,1);select(dlg,1,2);click(dlg,614)
        if not args.live:
            end=time.monotonic()+40
            while 'ready' not in control_text(dlg,620).lower():
                assert time.monotonic()<end,control_text(dlg,620)
                time.sleep(.2)
        click(dlg,2)
        def sample(phase):
            data=request(port,'/snapshot.jpg')
            im=Image.open(io.BytesIO(data)).convert('RGB');w,h=im.size
            counts=[]
            for a,b in ((.64,.81),(.81,1)):
                counts.append(sum(r>100 and g>100 for r,g,b in im.crop((0,int(h*a),w,int(h*b))).getdata()))
            current=state(port)
            expected=(True,True)
            if args.boundaries:
                position=int(current['position'])
                expected=next((pair for a,b,pair in ((3500,6500,(True,True)),(10000,12000,(False,False)),(15000,16500,(True,False))) if a<=position<=b),None)
            mismatch=expected is not None and tuple(n>=30 for n in counts)!=expected
            samples.append(dict(phase=phase,counts=counts,position=current['position'],rate=current['playbackrate'],expected=expected,mismatch=mismatch))
            if mismatch:
                (folder/f'failure-{len(samples)}.jpg').write_bytes(data)
            return min(counts)>=30
        def observe(phase,seconds):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                sample(phase);time.sleep(.035)
        def mark(value):
            if args.presenter_log:
                with args.presenter_log.open('a') as log:log.write(value+'\n')
        for cycle in range(args.cycles):
            user.PostMessageW(hwnd,0x111,888,0);time.sleep(.2);seek(port,1000)
            time.sleep(.5)
            if args.boundaries:
                seek(port,3000)
                mark('BEGIN timed')
                user.PostMessageW(hwnd,0x111,887,0)
                user.PostMessageW(hwnd,0x100,0x27,1)
                wait(port,lambda s:abs(float(s['playbackrate'])-3)<.01)
                deadline=time.monotonic()+12
                while int(state(port)['position'])<17500:
                    sample('timed-3x');time.sleep(.035)
                    assert time.monotonic()<deadline,'3x video failed to advance through cue changes'
                user.PostMessageW(hwnd,0x101,0x27,-1073741823)
                wait(port,lambda s:abs(float(s['playbackrate'])-1)<.01)
                mark('END')
                continue
            mark('BEGIN continuous')
            user.PostMessageW(hwnd,0x111,887,0)
            observe('normal',.6)
            user.PostMessageW(hwnd,0x100,0x27,1)
            observe('hold-transition',.5)
            wait(port,lambda s:abs(float(s['playbackrate'])-3)<.01)
            observe('held-3x',1.4)
            user.PostMessageW(hwnd,0x101,0x27,-1073741823)
            wait(port,lambda s:abs(float(s['playbackrate'])-1)<.01)
            observe('restored-1x',.7)
            mark('END')
        missing=[s for s in samples if s['mismatch']]
        if args.boundaries:
            assert {tuple(s['expected']) for s in samples if s['expected'] is not None}=={(True,True),(False,False),(True,False)},'cue transition coverage missing'
        frames=[];faults=[]
        if args.presenter_log:
            active=False
            for line in args.presenter_log.read_text().splitlines():
                if line.startswith('BEGIN'):active=True;continue
                if line=='END':active=False;continue
                if not active or not line.startswith('FRAME '):continue
                _,pid,t,start,stop,a,b,hr,mixed=line.split()
                t,start,stop,a,b=int(t),int(start),int(stop),int(a),int(b)
                expected=(True,True)
                if args.boundaries:
                    expected=next((pair for low,high,pair in ((3500,6500,(True,True)),(10000,12000,(False,False)),(15000,16500,(True,False))) if low*10000<=t<=high*10000),None)
                if expected is None:continue
                frame=dict(time=t,counts=[a,b],expected=expected,hr=hr)
                frames.append(frame)
                if min(a,b)<0 or mixed!='1' or int(hr,16)!=0 or not start<=t<stop or (a>8,b>8)!=expected:faults.append(frame)
            assert len(frames)>=args.cycles*15,('presenter probe unavailable or insufficient',len(frames))
        (folder/'result.json').write_text(json.dumps(dict(samples=samples,missing=len(missing),presenter_frames=frames,presenter_faults=faults),indent=2))
        if args.presenter_log:
            assert not faults,(faults[:5],folder)
        else:
            assert not missing,(len(missing),len(samples),folder)
        if args.kind=='pgs' and not args.boundaries:
            dlg=dialog(hwnd);click(dlg,615)
            # Returning to native single subtitles must keep the timestamp
            # basis already used by this embedded stream, including at 3x.
            for down,rate in ((True,3),(False,1)):
                user.PostMessageW(hwnd,0x100 if down else 0x101,0x27,1 if down else -1073741823)
                wait(port,lambda s:abs(float(s['playbackrate'])-rate)<.01)
                time.sleep(.3)
                im=Image.open(io.BytesIO(request(port,'/snapshot.jpg'))).convert('RGB');w,h=im.size
                assert sum(r>100 and g>100 for r,g,b in im.crop((0,int(h*.84),w,h)).getdata())>30, 'native single subtitle clock changed'
        evidence = f'{len(frames)} GPU frames after subtitle blending' if args.presenter_log else f'{len(samples)} snapshot samples'
        print(f'PASS {args.kind} vr{args.renderer} live={args.live} boundaries={args.boundaries}: {evidence}, {args.cycles} real Bilibili hold/release cycles; snapshot diagnostics={len(missing)}/{len(samples)}; {folder}',flush=True)
    finally:
        if hwnd:
            user.PostMessageW(hwnd,0x101,0x27,-1073741823)
            user.PostMessageW(hwnd,0x111,816,0)
        try:process.wait(15)
        except subprocess.TimeoutExpired:
            process.kill();process.wait();raise AssertionError('rate test close timeout')
    assert process.returncode==0,process.returncode

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--renderer',type=int,choices=(3,5),default=3)
    parser.add_argument('--kind',choices=('text','ass','pgs'),default='pgs')
    parser.add_argument('--live',action='store_true')
    parser.add_argument('--cycles',type=int,default=3)
    parser.add_argument('--boundaries',action='store_true')
    parser.add_argument('--unbuffered',action='store_true')
    parser.add_argument('--presenter-log',type=pathlib.Path,help='Requires a BridgeSubtitleProbe=true instrumentation build; checks actual blend submissions rather than the back buffer snapshot')
    parser.add_argument('--player',type=pathlib.Path)
    run(parser.parse_args())
