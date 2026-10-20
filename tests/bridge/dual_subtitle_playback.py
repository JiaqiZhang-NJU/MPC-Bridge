"""Native dual-subtitle regression. Self-made MJPEG/SRT fixtures, isolated INI.
Verify rendered pictures through the player's loopback snapshot endpoint.
"""
import ctypes
import io
import pathlib
import shutil
import socket
import struct
import subprocess
import time
import urllib.parse
import urllib.request
from PIL import Image
from quality_cache_playback import user, window, wait, message

ROOT=pathlib.Path(__file__).resolve().parents[2]

def video(path):
    def chunk(kind,data):return kind+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')
    def listing(kind,data):return chunk(b'LIST',kind+data)
    buf=io.BytesIO();Image.new('RGB',(640,360),(0,0,0)).save(buf,format='JPEG',quality=90)
    jpg=buf.getvalue();frames=240
    avih=struct.pack('<14I',100000,len(jpg)*10,0,0x10,frames,0,1,len(jpg),640,360,0,0,0,0)
    strh=struct.pack('<4s4sIHH8I4h',b'vids',b'MJPG',0,0,0,0,1,10,0,frames,len(jpg),0xffffffff,0,0,0,640,360)
    strf=struct.pack('<IiiHH4sIiiII',40,640,360,1,24,b'MJPG',640*360*3,0,0,0,0)
    head=listing(b'hdrl',chunk(b'avih',avih)+listing(b'strl',chunk(b'strh',strh)+chunk(b'strf',strf)))
    frame=chunk(b'00dc',jpg)
    index=b''.join(struct.pack('<4sIII',b'00dc',0x10,4+i*len(frame),len(jpg)) for i in range(frames))
    data=b'AVI '+head+listing(b'movi',frame*frames)+chunk(b'idx1',index)
    path.write_bytes(b'RIFF'+struct.pack('<I',len(data))+data)

def request(port,path):
    with urllib.request.urlopen(f'http://127.0.0.1:{port}'+path,timeout=5) as r:return r.read()

def edit(dlg,id,value):
    data=ctypes.create_unicode_buffer(str(value));result=ctypes.c_size_t()
    assert user.SendMessageTimeoutW(user.GetDlgItem(dlg,id),0x000c,0,ctypes.addressof(data),2,3000,ctypes.byref(result))

def dialog(hwnd):
    user.PostMessageW(hwnd,0x111,50005,0);end=time.monotonic()+5
    while time.monotonic()<end:
        popup=user.GetLastActivePopup(hwnd)
        if popup and popup!=hwnd and user.GetDlgItem(popup,600):return popup
        time.sleep(.05)
    raise AssertionError('dual subtitle dialog unavailable')

def click(dlg,id):user.PostMessageW(dlg,0x111,id,0);time.sleep(.5)

def picture(port,folder,name):
    data=request(port,'/snapshot.jpg');(folder/(name+'.jpg')).write_bytes(data)
    image=Image.open(io.BytesIO(data)).convert('RGB');w,h=image.size
    counts=[sum(1 for r,g,b in (image.getpixel((x,y)) for x in range(w)) if r>100 and g>100) for y in range(h)]
    return counts,h

def seek(port,ms):
    request(port,'/command.html?wm_command=-1&position='+urllib.parse.quote(f'00:00:{ms//1000:02d}.{ms%1000:03d}'))
    wait(port,lambda s:abs(int(s['position'])-ms)<150);time.sleep(.5)

def run(renderer):
    folder=ROOT/'_bin/bridge/dual-qa'/f'vr{renderer}-{time.time_ns()}';folder.mkdir(parents=True)
    movie=folder/'fixture.avi';video(movie)
    primary=folder/'原文 a.srt';secondary=folder/'译文 b.srt'
    primary.write_text('1\n00:00:01,000 --> 00:00:08,000\nA1: Learn languages\nwith two subtitle files.\n\n2\n00:00:14,000 --> 00:00:18,000\nA2: Another sentence.\n',encoding='utf-8-sig')
    secondary.write_text('1\n00:00:02,000 --> 00:00:04,000\nB1: 第一段译文\n\n2\n00:00:04,000 --> 00:00:09,000\nB2: 第二段译文\n不同的分段也能对照\n',encoding='utf-16')
    player=folder/'subtitle-player.exe';shutil.copy2(ROOT/'_bin/mpc-be_x64/mpc-be64.exe',player)
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    player.with_suffix('.ini').write_text(f'[Audio]\nMute=1\n[Video]\nVideoRenderer={renderer}\n[Settings]\nSnapShotSubtitles=1\n[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nEnablePreview=1\nPort={port}\n',encoding='utf-16')
    process=subprocess.Popen([str(player),str(movie),'/new','/minimized']);hwnd=None
    try:
        wait(port,lambda s:int(s.get('position','0'))>500);hwnd=window(process.pid);assert hwnd
        try: request(port,'/snapshot.jpg')
        except urllib.error.HTTPError: pass # no surface is a valid unavailable snapshot, never a crash
        assert process.poll() is None,'minimized snapshot crashed the player'
        user.ShowWindow(ctypes.c_void_p(hwnd),9);time.sleep(1)
        user.SetWindowPos(ctypes.c_void_p(hwnd),ctypes.c_void_p(-1),100,100,800,600,0x0040)
        user.SetForegroundWindow(ctypes.c_void_p(hwnd));time.sleep(1)
        user.PostMessageW(hwnd,0x111,888,0);wait(port,lambda s:s['state']=='1')
        dlg=dialog(hwnd);edit(dlg,600,primary);edit(dlg,601,secondary);click(dlg,614)
        assert user.GetLastActivePopup(hwnd)==dlg,'apply displayed an error'
        click(dlg,2)
        seek(port,3000);counts,h=picture(port,folder,'both-bottom')
        assert sum(counts[int(h*.64):int(h*.81)])>50,('primary missing',h)
        assert sum(counts[int(h*.81):])>30,('secondary missing',h)
        seek(port,1500);counts,_=picture(port,folder,'primary-only')
        assert sum(counts[int(h*.81):])<20,'secondary appeared early'
        seek(port,11000);counts,_=picture(port,folder,'gap');assert sum(counts)<30,'old subtitles remain'
        dlg=dialog(hwnd);edit(dlg,607,2000);click(dlg,614);click(dlg,2)
        seek(port,3000);counts,_=picture(port,folder,'delayed-secondary')
        assert sum(counts[int(h*.81):])<20,'independent delay failed'
        dlg=dialog(hwnd);edit(dlg,607,0);message(user.GetDlgItem(dlg,612),0x14e,1);click(dlg,614);click(dlg,2)
        seek(port,5000);counts,_=picture(port,folder,'top-bottom')
        assert sum(counts[:int(h*.45)])>30 and sum(counts[int(h*.55):])>30,'top/bottom layout failed'
        user.PostMessageW(hwnd,0x111,1171,0);time.sleep(.5)
        counts,_=picture(port,folder,'hidden');assert sum(counts)<30,'global toggle did not hide both'
        user.PostMessageW(hwnd,0x111,1171,0);time.sleep(.5)
        counts,_=picture(port,folder,'restored');assert sum(counts)>50,'global toggle did not restore subtitles'
        # A malformed replacement must leave the previously parsed provider alive.
        invalid=folder/'invalid.srt';invalid.write_text('not a subtitle',encoding='utf-8')
        dlg=dialog(hwnd);edit(dlg,601,invalid);click(dlg,614)
        popup=user.GetLastActivePopup(hwnd)
        assert popup and popup!=dlg,'invalid source was accepted'
        user.PostMessageW(popup,0x10,0,0);time.sleep(.3);click(dlg,2)
        counts,_=picture(port,folder,'after-invalid');assert sum(counts)>50,'invalid replacement removed valid subtitles'
        # Fullscreen and a frame step must preserve the two lanes.
        user.PostMessageW(hwnd,0x111,830,0);time.sleep(.6)
        user.PostMessageW(hwnd,0x111,891,0);time.sleep(.3)
        user.PostMessageW(hwnd,0x111,830,0);time.sleep(.6)
        seek(port,5000);counts,_=picture(port,folder,'after-fullscreen');assert sum(counts)>50
        if '--stress' in __import__('sys').argv:
            import psutil
            native=psutil.Process(process.pid);before=native.memory_info().rss
            dlg=dialog(hwnd)
            for n in range(100):
                edit(dlg,607,n%2*500);click(dlg,614)
                assert user.GetLastActivePopup(hwnd)==dlg,('stress apply failed',n)
            click(dlg,2);after=native.memory_info().rss
            assert after-before<64*1024*1024,('working set grew by',after-before)
            print(f'PASS: 100 transactional replacements; RSS delta {after-before} bytes',flush=True)
        dlg=dialog(hwnd);edit(dlg,606,-500);click(dlg,613);click(dlg,614);click(dlg,2)
        seek(port,3000);picture(port,folder,'swapped')
        dlg=dialog(hwnd);click(dlg,615);seek(port,3000);picture(port,folder,'secondary-disabled')
        print(f'PASS renderer {renderer}: real dual SRT pictures, UTF encodings, multiple lines, gaps, independent delay, layouts, swap; {folder}',flush=True)
    finally:
        if hwnd:user.PostMessageW(hwnd,0x111,816,0)
        try:process.wait(10)
        except subprocess.TimeoutExpired:process.kill();process.wait();raise AssertionError('player close hung')
    assert process.returncode==0,process.returncode

def run_disabled():
    folder=ROOT/'_bin/bridge/dual-qa'/f'disabled-{time.time_ns()}';folder.mkdir(parents=True)
    movie=folder/'fixture.avi';video(movie)
    player=folder/'disabled-player.exe';shutil.copy2(ROOT/'_bin/mpc-be_x64/mpc-be64.exe',player)
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    player.with_suffix('.ini').write_text(f'[Audio]\nMute=1\n[Video]\nVideoRenderer=3\n[Settings]\nSubtitleRenderer=0\n[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nPort={port}\n',encoding='utf-16')
    process=subprocess.Popen([str(player),str(movie),'/new','/minimized']);hwnd=None
    try:
        wait(port,lambda s:int(s.get('position','0'))>500);hwnd=window(process.pid);assert hwnd
        user.PostMessageW(hwnd,0x111,50005,0);time.sleep(.8)
        popup=user.GetLastActivePopup(hwnd)
        assert popup and popup!=hwnd and not user.GetDlgItem(popup,600),'unsupported renderer entered dual mode'
        user.PostMessageW(popup,0x10,0,0);time.sleep(.2)
        print('PASS: disabled internal subtitle renderer gives an explanatory message and cannot enter dual mode',flush=True)
    finally:
        if hwnd:user.PostMessageW(hwnd,0x111,816,0)
        try:process.wait(10)
        except subprocess.TimeoutExpired:process.kill();process.wait();raise AssertionError('disabled renderer exit hung')
    assert process.returncode==0

if __name__=='__main__':
    import sys
    if '--disabled' in sys.argv:run_disabled()
    else:
        for vr in ([int(sys.argv[1])] if len(sys.argv)>1 and sys.argv[1].isdigit() else [3,5]):run(vr)
