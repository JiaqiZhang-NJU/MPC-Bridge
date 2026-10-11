"""M0 gate: the unchanged native player must consume both cached MP4 resources."""
import ctypes
import http.client
import pathlib
import re
import shutil
import socket
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from cache_integration import ROOT, query, start, stop

MEDIA = ROOT / '_bin/bridge/cache-qa/media'
class Files(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def do_GET(self):
        name = self.path.lstrip('/')
        if name not in ('30016.mp4', '30216.m4a'): self.send_error(404); return
        file = MEDIA / name
        size = file.stat().st_size
        first, last = map(int, self.headers['Range'].removeprefix('bytes=').split('-'))
        self.send_response(206)
        self.send_header('Content-Range', f'bytes {first}-{last}/{size}')
        self.send_header('Content-Length', str(last - first + 1))
        self.send_header('ETag', '"' + name + '-v1"')
        self.send_header('Connection', 'close')
        self.end_headers()
        try:
            with file.open('rb') as stream:
                stream.seek(first)
                self.wfile.write(stream.read(last - first + 1))
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

def state(port):
    connection = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
    connection.request('GET', '/variables.html')
    html = connection.getresponse().read().decode('utf-8')
    connection.close()
    return dict(re.findall(r'<p id="([^"]+)">([^<]*)</p>', html))

def main():
    server = ThreadingHTTPServer(('127.0.0.1', 0), Files)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    video, vr, vd = start(base + '/30016.mp4', 'native-video')
    audio, ar, ad = start(base + '/30216.m4a', 'native-audio')
    folder = ROOT / '_bin/bridge/cache-qa' / ('player-' + str(time.time_ns()))
    folder.mkdir(parents=True, exist_ok=True)
    player = folder / 'cache-player.exe'
    shutil.copy2(ROOT / '_bin/mpc-be_x64/mpc-be64.exe', player)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0)); port = sock.getsockname()[1]
    player.with_suffix('.ini').write_text(f'[Audio]\nMute=1\nVolume=30\n[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nPort={port}\n', encoding='utf-16')
    process = subprocess.Popen([str(player), vr, '/dub', ar, '/new', '/minimized'])
    try:
        deadline = time.monotonic() + 30
        current = {}
        while time.monotonic() < deadline:
            try: current = state(port)
            except (OSError, http.client.HTTPException): time.sleep(.2); continue
            if int(current.get('position', '0')) > 2000: break
            time.sleep(.2)
        assert int(current.get('duration', '0')) > 500000, current
        assert int(current.get('position', '0')) > 2000, current
        for probe in (video, audio):
            probe.stdin.write('status\n'); probe.stdin.flush()
            output = probe.stdout.readline().strip()
            assert output.startswith('STATUS '), output
            fields = output.split()
            assert float(fields[4]) >= 2, f'index/common forward coverage unavailable: {output}'
        # A cached range must be byte-for-byte identical to the actual fragmented source.
        assert query(vr, 'bytes=500000-510000')[2] == (MEDIA / '30016.mp4').read_bytes()[500000:510001]
        print('PASS: native dual-stream MP4 playback, duration/position, Bento4 time-index coverage and exact cached media bytes')
    finally:
        # Address only the window owned by this fresh test process.
        user32 = ctypes.windll.user32
        user32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
        callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def close(hwnd, _):
            pid = ctypes.c_ulong()
            user32.GetWindowThreadProcessId(ctypes.c_void_p(hwnd), ctypes.byref(pid))
            if pid.value == process.pid and user32.IsWindowVisible(ctypes.c_void_p(hwnd)):
                user32.PostMessageW(hwnd, 0x111, 816, 0)
            return True
        user32.EnumWindows(callback_type(close), 0)
        try: process.wait(10)
        except subprocess.TimeoutExpired: process.kill(); process.wait(); raise AssertionError('native close hung')
        stop(video, vd); stop(audio, ad); server.shutdown()
    assert process.returncode == 0, process.returncode

if __name__ == '__main__': main()
