"""Controlled origin tests. Python is test tooling, never a player runtime dependency."""
import concurrent.futures
import http.client
import pathlib
import random
import subprocess
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = bytes(range(256)) * (20 * 1024 * 1024 // 256) + b"tail"
lock = threading.Lock()
requests = []
active = peak = 0

class Origin(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args):
        pass
    def do_GET(self):
        global active, peak
        spec = self.headers.get('Range', '')
        assert self.headers.get('Referer') == 'https://www.bilibili.com/'
        assert self.headers.get('Accept-Encoding') == 'identity'
        assert self.headers.get('User-Agent') == 'MPC-Bridge-test'
        with lock:
            active += 1
            peak = max(peak, active)
            requests.append((self.path, spec))
        try:
            first, last = map(int, spec.removeprefix('bytes=').split('-'))
            if self.path in ('/forbidden','/gone'):
                self.send_error(403 if self.path=='/forbidden' else 410); return
            if self.path=='/redirect':
                self.send_response(302); self.send_header('Location','/media'); self.send_header('Content-Length','0'); self.end_headers(); return
            if self.path == '/slow' and first != last:
                time.sleep(20)
            self.send_response(200 if self.path == '/ignore' else 206)
            self.send_header('Content-Range', f'bytes {first + (self.path == "/bad")}-{last}/{len(DATA)}')
            self.send_header('Content-Length', str(last - first + 1))
            self.send_header('ETag', '"fixture-v2"' if self.path == '/changed' and first != last else '"fixture-v1"')
            self.send_header('Connection', 'close')
            self.end_headers()
            body = DATA[first:last + 1]
            if self.path == '/short' and first != last: body = body[:len(body)//2]
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            with lock:
                active -= 1

def query(route, range=None, method='GET'):
    url = urllib.parse.urlsplit(route)
    connection = http.client.HTTPConnection(url.hostname, url.port, timeout=12)
    connection.request(method, url.path, headers={'Range': range} if range else {})
    response = connection.getresponse()
    content = response.read()
    status, headers = response.status, dict(response.getheaders())
    connection.close()
    return status, headers, content

def start(origin, suffix):
    folder = ROOT / '_bin/bridge/cache-qa' / suffix
    folder.mkdir(parents=True, exist_ok=True)
    process = subprocess.Popen([str(ROOT / '_bin/bridge/cache-probe.exe'), origin, str(folder), str(16 * 1024 * 1024)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    line = process.stdout.readline().rstrip('\r\n')
    assert line.startswith('ROUTE '), line
    return process, line[6:], folder

def stop(process, folder):
    begin = time.monotonic()
    process.communicate('quit\n', timeout=3)
    assert process.returncode == 0
    assert time.monotonic() - begin < 1.5, 'shutdown latency'
    assert not list(folder.iterdir()), 'session cache was not removed'

def main():
    server = ThreadingHTTPServer(('127.0.0.1', 0), Origin)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f'http://127.0.0.1:{server.server_port}'
    p, route, folder = start(origin + '/media', 'normal')
    try:
        assert route.startswith('http://127.0.0.1:')
        status, headers, body = query(route, method='HEAD')
        assert status == 200 and headers['Content-Length'] == str(len(DATA)) and not body
        rng = random.Random(1978)
        for _ in range(30):
            first = rng.randrange(len(DATA) - 8192)
            last = first + rng.randrange(1, 8192)
            status, headers, body = query(route, f'bytes={first}-{last}')
            assert status == 206 and body == DATA[first:last + 1]
            assert headers['Content-Range'] == f'bytes {first}-{last}/{len(DATA)}'
        for spec, expected in [('bytes=-73', DATA[-73:]), ('bytes=20971520-', DATA[20971520:])]:
            assert query(route, spec)[2] == expected
        assert query(route, f'bytes={len(DATA)}-')[0] == 416
        for spec in ('bytes=0-1,4-5', 'bytes=a-b', 'bytes=18446744073709551616-', 'items=0-1'):
            assert query(route, spec)[0] == 400
        assert query(route + '/invalid', 'bytes=0-3')[0] == 404
        assert query(route, method='POST')[0] == 405
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            values = list(pool.map(lambda _: query(route, 'bytes=18000000-18001000')[2], range(4)))
        assert all(v == DATA[18000000:18001001] for v in values)
        # Bring one previously uncached block into memory, then prove the next read is disk-only.
        assert query(route, 'bytes=19000000-19004000')[2] == DATA[19000000:19004001]
        with lock:
            before = len([r for r in requests if r[1].startswith('bytes=18874368-')])
        assert query(route, 'bytes=19000000-19004000')[2] == DATA[19000000:19004001]
        with lock:
            after = len([r for r in requests if r[1].startswith('bytes=18874368-')])
        assert before == after, 'warm range fetched origin'
        disk = sum(f.stat().st_size for f in folder.rglob('*.block'))
        assert disk <= 16 * 1024 * 1024
    finally:
        stop(p, folder)
    for suffix in ('ignore', 'bad', 'forbidden', 'gone'):
        p, route, folder = start(origin + '/' + suffix, suffix)
        assert not route, 'invalid range origin accepted'
        stop(p, folder)
    for suffix in ('changed', 'short'):
        p, route, folder = start(origin + '/' + suffix, suffix)
        assert route
        assert query(route, 'bytes=18000000-18004000')[0] == 502
        assert not list(folder.rglob('*.block')), 'unvalidated/truncated bytes entered cache'
        stop(p, folder)
    p,route,folder=start(origin+'/redirect','redirect')
    try: assert query(route,'bytes=1048576-1052672')[2]==DATA[1048576:1052673]
    finally:stop(p,folder)
    p,route,folder=start(origin+'/media','write-failure')
    try:
        p.stdin.write('idle\n'); p.stdin.flush(); assert p.stdout.readline().strip()=='IDLE'
        session=next(folder.glob('session-*'))
        # A directory at the exact destination injects a file-open failure without touching user data.
        destination=session/'1-18.block'; destination.mkdir()
        assert query(route,'bytes=18874368-18875000')[0]==502
    finally:stop(p,folder)
    p, route, folder = start(origin + '/slow', 'slow')
    time.sleep(.3)
    stop(p, folder)
    server.shutdown()
    assert peak <= 2, peak
    print('PASS: exact ranges, suffix/open ranges, invalid routes/ranges, concurrent dedup, quota, warm hits, upstream validation, slow cancellation and cleanup')

if __name__ == '__main__':
    main()
