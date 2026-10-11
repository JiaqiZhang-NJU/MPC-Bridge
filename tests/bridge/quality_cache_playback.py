"""Deterministic native quality switching/state tests with local media and a fixture extractor.

Fixture quality labels exercise selection; real resolution is checked separately with public yt-dlp samples.
"""
import ctypes
import http.client
import json
import pathlib
import shutil
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from cache_integration import ROOT
from cache_native_playback import MEDIA, state

user = ctypes.windll.user32
for name, args, result in [
    ('GetMenu', [ctypes.c_void_p], ctypes.c_void_p),
    ('GetSubMenu', [ctypes.c_void_p, ctypes.c_int], ctypes.c_void_p),
    ('GetMenuItemCount', [ctypes.c_void_p], ctypes.c_int),
    ('GetMenuItemID', [ctypes.c_void_p, ctypes.c_int], ctypes.c_uint),
    ('GetMenuState', [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint], ctypes.c_uint),
    ('PostMessageW', [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t], ctypes.c_bool),
    ('GetDlgItem', [ctypes.c_void_p, ctypes.c_int], ctypes.c_void_p),
    ('GetLastActivePopup', [ctypes.c_void_p], ctypes.c_void_p),
]:
    function = getattr(user, name); function.argtypes = args; function.restype = result
user.SendMessageTimeoutW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_size_t)]
user.SendMessageTimeoutW.restype = ctypes.c_size_t
CB = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

def window(pid):
    result = []
    def find(hwnd, _):
        candidate = ctypes.c_ulong()
        user.GetWindowThreadProcessId(ctypes.c_void_p(hwnd), ctypes.byref(candidate))
        if candidate.value == pid and user.GetMenu(hwnd): result.append(hwnd)
        return True
    user.EnumWindows(CB(find), 0)
    return result[0] if result else None

def message(hwnd, command, argument):
    result = ctypes.c_size_t()
    assert user.SendMessageTimeoutW(hwnd, command, argument, 0, 2, 3000, ctypes.byref(result)), 'UI message timeout'

def wait(port, condition, timeout=20):
    deadline = time.monotonic() + timeout
    current = {}
    while time.monotonic() < deadline:
        try:
            current = state(port)
            if condition(current): return current
        except (OSError, http.client.HTTPException): pass
        time.sleep(.1)
    raise AssertionError({k: current.get(k) for k in ('state','position','duration','onlineheight','playbackrate','cacheforward','cachebytes')})

def quality_menu(hwnd):
    root = user.GetMenu(hwnd)
    candidates = [user.GetSubMenu(root, i) for i in range(user.GetMenuItemCount(root))]
    menus = [m for m in candidates if user.GetMenuItemID(m, 0) == 50100]
    assert len(menus) == 1, 'quality root duplicate/missing'
    message(hwnd, 0x117, menus[0])
    return menus[0]

class Files(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    failures = set()
    count = 0
    paths = {}
    offline = False
    def log_message(self, *args): pass
    def do_GET(self):
        type(self).count += 1
        type(self).paths[self.path] = type(self).paths.get(self.path, 0) + 1
        if type(self).offline or self.path in type(self).failures:
            self.send_error(410); return
        file = MEDIA / ('30216.m4a' if self.path.endswith('m4a') else '30016.mp4')
        size = file.stat().st_size
        spec = self.headers.get('Range', '')
        first, last = (0, size - 1)
        if spec:
            a, b = spec.removeprefix('bytes=').split('-'); first = int(a); last = min(int(b) if b else size - 1, size - 1)
        self.send_response(206 if spec else 200)
        self.send_header('Content-Range', f'bytes {first}-{last}/{size}')
        self.send_header('Content-Length', str(last - first + 1))
        self.send_header('Content-Type', 'video/mp4')
        self.send_header('ETag', '"fixture-v1"')
        self.send_header('Connection', 'close')
        self.end_headers()
        try:
            with file.open('rb') as stream:
                stream.seek(first)
                remaining = last - first + 1
                while remaining:
                    data = stream.read(min(65536, remaining)); self.wfile.write(data); remaining -= len(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

def main():
    server = ThreadingHTTPServer(('127.0.0.1', 0), Files); server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    folder = ROOT / '_bin/bridge/quality-qa' / str(time.time_ns()); folder.mkdir(parents=True)
    player = folder / 'quality-player.exe'; shutil.copy2(ROOT / '_bin/mpc-be_x64/mpc-be64.exe', player)
    extractor = folder / 'fixture-parser.exe'; shutil.copy2(ROOT / '_bin/bridge/fixture-parser.exe', extractor)
    formats = [dict(format_id=str(height),height=height,width=640,fps=30,vcodec='avc1.64001E',acodec='none',ext='mp4',protocol='http',container='mp4_dash',tbr=350,url=base+f'/{height}.mp4') for height in (1080,720,480,360)]
    formats.append(dict(format_id='audio',vcodec='none',acodec='mp4a.40.2',ext='m4a',protocol='http',container='m4a_dash',abr=64,url=base+'/audio.m4a'))
    extractor.with_suffix('.json').write_text(json.dumps(dict(id='fixture',title='Controlled cache playback',duration=554.117,formats=formats)), encoding='utf-8')
    with socket.socket() as sock: sock.bind(('127.0.0.1',0)); port = sock.getsockname()[1]
    cache = folder / 'cache'
    preset = json.dumps(dict(schemaVersion=1,id='rate-test',displayName='Rate test',bindings=[dict(key='R',action='rate.relative',args=dict(step=.5))]), separators=(',',':'))
    player.with_suffix('.ini').write_text(f'[MPC-Bridge]\nShortcutProfile=custom\nCustomProfile={preset}\n[Audio]\nMute=1\nVolume=30\n[OnlineServices]\nYDLEnable=1\nYDLExePath={extractor}\nYDLVideoCodec=H264\nYDLAudioCodec=AAC\nYDLMaxHeight=1080\n[MPC-Bridge Cache]\nDirectory={cache}\n[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nPort={port}\n', encoding='utf-16')
    process = subprocess.Popen([str(player),'https://www.bilibili.com/video/BV13x41117TL','/new','/minimized'])
    hwnd = None
    try:
        initial = wait(port, lambda s: int(s['position']) > 1500 and s['onlineheight'] == '1080')
        hwnd = window(process.pid); assert hwnd
        menu = quality_menu(hwnd); count = user.GetMenuItemCount(menu)
        assert user.GetMenuState(menu, 50200, 0) & 8
        message(hwnd, 0x117, menu); assert user.GetMenuItemCount(menu) == count
        user.PostMessageW(hwnd, 0x100, ord('R'), 1); user.PostMessageW(hwnd, 0x101, ord('R'), -1073741823)
        wait(port, lambda s: abs(float(s['playbackrate']) - 1.5) < .01)
        user.PostMessageW(hwnd, 0x111, 888, 0)
        paused = wait(port, lambda s: s['state'] == '1')
        old_position = int(paused['position'])
        user.PostMessageW(hwnd, 0x111, 50201, 0)
        switched = wait(port, lambda s: s['onlineheight'] == '720' and s['state'] == '1' and abs(float(s['playbackrate'])-1.5)<.01)
        assert abs(int(switched['position']) - old_position) < 1500
        assert switched['volumelevel'] == paused['volumelevel'] and switched['muted'] == paused['muted']
        wait(port, lambda s: float(s['cacheforward']) >= 45)
        before = Files.count; time.sleep(1); assert Files.count == before, 'paused preload did not stop at target'
        Files.offline = True
        before_position = int(state(port)['position'])
        user.PostMessageW(hwnd, 0x111, 887, 0)
        time.sleep(5)
        offline = state(port)
        assert int(offline['position']) - before_position >= 6500, 'cached playback stalled during a 5 second outage'
        Files.offline = False
        user.PostMessageW(hwnd, 0x111, 888, 0)
        wait(port, lambda s: s['state']=='1')
        # Change three times while opening: the last selection wins and retains the paused normal rate.
        for command in (50200,50202,50201): user.PostMessageW(hwnd,0x111,command,0)
        time.sleep(2) # let queued commands complete; do not accept the pre-switch 720p state
        wait(port, lambda s: s['onlineheight']=='720' and s['state']=='1' and abs(float(s['playbackrate'])-1.5)<.01)
        # An unavailable target fails and rolls back to the prior representation.
        Files.failures.add('/360.mp4'); user.PostMessageW(hwnd,0x111,50203,0)
        deadline = time.monotonic()+10
        while Files.paths.get('/360.mp4',0)==0 and time.monotonic()<deadline: time.sleep(.05)
        assert Files.paths.get('/360.mp4',0)>0, 'unavailable target was not attempted'
        time.sleep(1)
        wait(port, lambda s: s['onlineheight']=='720' and s['state']=='1' and abs(float(s['playbackrate'])-1.5)<.01)
        # Runtime settings are real controls and persist after normal exit.
        for _ in range(3):
            user.PostMessageW(hwnd,0x100,ord('R'),1); user.PostMessageW(hwnd,0x101,ord('R'),-1073741823)
            time.sleep(.15)
        wait(port,lambda s:abs(float(s['playbackrate'])-3)<.01)
        wait(port,lambda s:float(s['cacheforward'])>=90)
        time.sleep(.5); before=Files.count; time.sleep(1)
        assert Files.count==before, 'paused 3x preload continued past its 90 second target'
        Files.offline=True; before_position=int(state(port)['position'])
        user.PostMessageW(hwnd,0x111,887,0); time.sleep(5)
        assert int(state(port)['position'])-before_position>=13000, 'cached 3x playback stalled during outage'
        Files.offline=False; user.PostMessageW(hwnd,0x111,888,0)
        wait(port,lambda s:s['state']=='1')
        user.PostMessageW(hwnd,0x111,50101,0)
        deadline = time.monotonic()+5
        while time.monotonic()<deadline:
            popup = user.GetLastActivePopup(hwnd)
            if popup != hwnd and user.GetDlgItem(popup,201) and user.GetDlgItem(popup,2): break
            time.sleep(.05)
        else: raise AssertionError('cache dialog missing')
        time.sleep(.5) # finish the framework's initial dialog data exchange
        # SetWindowText on another process only changes the cached caption for some controls.
        # WM_SETTEXT is marshalled below WM_USER and reaches the native EDIT procedure.
        value = ctypes.create_unicode_buffer('45'); message_result = ctypes.c_size_t()
        assert user.SendMessageTimeoutW(user.GetDlgItem(popup,201),0x000C,0,ctypes.cast(value,ctypes.c_void_p).value,2,3000,ctypes.byref(message_result))
        text = ctypes.create_unicode_buffer(128)
        assert user.SendMessageTimeoutW(user.GetDlgItem(popup,201),0x000D,128,ctypes.cast(text,ctypes.c_void_p).value,2,3000,ctypes.byref(message_result))
        assert text.value == '45', text.value
        user.PostMessageW(user.GetDlgItem(popup,1),0x00F5,0,0) # BM_CLICK on the actual Save button
        time.sleep(.5)
        assert not user.IsWindow(ctypes.c_void_p(popup)), 'cache settings dialog did not close'
        saved_ini = player.with_suffix('.ini').read_bytes()
        assert 'ForwardSeconds=45' in saved_ini.decode('utf-16' if saved_ini.startswith(b'\xff\xfe') else 'utf-8-sig'), 'value was not saved by the dialog'
        user.PostMessageW(hwnd,0x111,803,0) # close media
        wait(port, lambda s: s['onlineheight']=='' and float(s['cachebytes'])==0)
        menu = quality_menu(hwnd)
        assert user.GetMenuState(menu,50200,0)==0xffffffff, 'stale quality options after close'
    finally:
        if not hwnd: hwnd = window(process.pid)
        if hwnd: user.PostMessageW(hwnd,0x111,816,0)
        try: process.wait(10)
        except subprocess.TimeoutExpired: process.kill(); process.wait(); raise AssertionError('player exit hung')
        server.shutdown()
    assert process.returncode == 0, process.returncode
    ini = player.with_suffix('.ini').read_bytes()
    assert 'ForwardSeconds=45' in ini.decode('utf-16' if ini.startswith(b'\xff\xfe') else 'utf-8-sig')
    assert not list(cache.glob('session-*')), 'session cache leaked'
    print('PASS: independent quality menu, exact selection, paused/rate/volume/mute restoration, bounded paused preload, 5 second origin outages at 1.5x and 3x with proven 45/90 second coverage, latest selection, failure rollback, settings persistence and close cleanup')

if __name__=='__main__': main()
