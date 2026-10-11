"""Opt-in public native playback/switching; fresh process/INI, no account cookies."""
import argparse
import ctypes
import json
import pathlib
import shutil
import socket
import subprocess
import time
from cache_integration import ROOT
from quality_cache_playback import user, window, quality_menu, wait, state

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('url')
    parser.add_argument('--parser',type=pathlib.Path,default=ROOT/'_bin/bridge/online/yt-dlp.exe')
    options=parser.parse_args()
    assert options.url.startswith('https://www.bilibili.com/video/') or options.url.startswith('https://www.youtube.com/watch?')
    folder=ROOT/'_bin/bridge/public-quality'/str(time.time_ns()); folder.mkdir(parents=True)
    player=folder/'quality-player.exe'; shutil.copy2(ROOT/'_bin/mpc-be_x64/mpc-be64.exe',player)
    with socket.socket() as sock: sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
    player.with_suffix('.ini').write_text(f'[Audio]\nMute=1\nVolume=30\n[OnlineServices]\nYDLEnable=1\nYDLExePath={options.parser.resolve()}\nYDLVideoCodec=H264\nYDLAudioCodec=AAC\nYDLMaxHeight=1080\n[MPC-Bridge Cache]\nDirectory={folder / "cache"}\n[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nPort={port}\n',encoding='utf-16')
    process=subprocess.Popen([str(player),options.url,'/new','/minimized'])
    hwnd=None; results=[]
    try:
        initial=wait(port,lambda s:int(s['position'])>1500,timeout=60)
        hwnd=window(process.pid); assert hwnd
        heights=[int(v.strip()) for v in initial['onlineheights'].split(',')]
        assert len(heights)>=2,initial
        user.PostMessageW(hwnd,0x111,888,0)
        paused=wait(port,lambda s:s['state']=='1'); position=int(paused['position'])
        for target in (heights[1],heights[0]):
            menu=quality_menu(hwnd)
            command=50200+heights.index(target)
            user.PostMessageW(hwnd,0x111,command,0)
            current=wait(port,lambda s:s['onlineheight']==str(target) and s['state']=='1',timeout=60)
            assert abs(int(current['position'])-position)<1500,current
            menu=quality_menu(hwnd); assert user.GetMenuState(menu,command,0)&8
            user.PostMessageW(hwnd,0x111,887,0)
            before=int(current['position'])
            current=wait(port,lambda s:int(s['position'])>before+1000,timeout=20)
            results.append(dict(height=target,position=int(current['position']),cacheforward=current['cacheforward'],cachebytes=current['cachebytes']))
            user.PostMessageW(hwnd,0x111,888,0)
            position=int(wait(port,lambda s:s['state']=='1')['position'])
        (folder/'result.json').write_text(json.dumps(dict(url=options.url,duration=initial['duration'],heights=heights,switches=results),indent=2),encoding='utf-8')
        print('PASS: public native quality switching and advancing playback',json.dumps(results))
    finally:
        hwnd=hwnd or window(process.pid)
        if hwnd:user.PostMessageW(hwnd,0x111,816,0)
        try:process.wait(12)
        except subprocess.TimeoutExpired:process.kill();process.wait();raise AssertionError('native exit hung')
    assert process.returncode==0,process.returncode
    assert not list((folder/'cache').glob('session-*'))

if __name__=='__main__':main()
