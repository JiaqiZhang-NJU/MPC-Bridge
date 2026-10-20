"""Real MKV embedded-text + external SRT regression; no external muxer required."""
import ctypes
import io
import pathlib
import shutil
import socket
import struct
import subprocess
import sys
import time
from PIL import Image
from dual_subtitle_playback import ROOT, dialog, edit, click, picture, seek, request
from quality_cache_playback import user, window, wait


def query(hwnd, command, argument=0):
    result = ctypes.c_size_t()
    assert user.SendMessageTimeoutW(hwnd, command, argument, 0, 2, 3000, ctypes.byref(result))
    return result.value


def mkv(path, ass=False):
    def size(n):
        for width in range(1, 9):
            if n < (1 << (7 * width)) - 1:
                return ((1 << (7 * width)) | n).to_bytes(width, 'big')
        raise ValueError(n)
    def elem(tag, data):
        return bytes.fromhex(tag) + size(len(data)) + data
    def uint(tag, value):
        return elem(tag, value.to_bytes(max(1, (value.bit_length()+7)//8), 'big'))
    def text(tag, value):
        return elem(tag, value.encode())
    header = elem('1a45dfa3', uint('4286', 1)+uint('42f7', 1)+uint('42f2', 4)+uint('42f3', 8)
                  +text('4282', 'matroska')+uint('4287', 4)+uint('4285', 2))
    info = elem('1549a966', uint('2ad7b1', 1000000)+elem('4489', struct.pack('>d', 24000))
                +text('4d80', 'mpc-bridge test')+text('5741', 'mpc-bridge test'))
    tracks = elem('ae', uint('d7', 1)+uint('73c5', 1)+uint('83', 1)+text('86', 'V_MJPEG')
                  +uint('23e383', 100000000)+elem('e0', uint('b0', 640)+uint('ba', 360)))
    private = '''[Script Info]
ScriptType: v4.00+
PlayResX: 640
PlayResY: 360
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,24,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
'''
    for number, name, language in [(2, 'English', 'eng'), (3, 'Chinese', 'chi')]:
        entry = uint('d7', number)+uint('73c5', number)+uint('83', 17)+text('536e', name)+text('22b59c', language)
        entry += text('86', 'S_TEXT/ASS' if ass else 'S_TEXT/UTF8')
        if ass:
            entry += elem('63a2', private.encode())
        tracks += elem('ae', entry)
    buf = io.BytesIO()
    Image.new('RGB', (640, 360), (0, 0, 0)).save(buf, format='JPEG', quality=90)
    cues = [(1000, 8000, 2, 'Original first line'), (14000, 18000, 2, 'Original second line'),
            (2000, 4000, 3, '第一段译文'), (4000, 9000, 3, '第二段译文')]
    clusters, index = [], []
    def seek_head(position):
        return elem('114d9b74', elem('4dbb', elem('53ab', bytes.fromhex('1c53bb6b'))
                                    +elem('53ac', position.to_bytes(8, 'big'))))
    placeholder = seek_head(0)
    segment_prefix = placeholder+info+elem('1654ae6b', tracks)
    for second in range(24):
        entries = [(second*1000+i*100, 1, elem('a3', b'\x81'+struct.pack('>hB', i*100, 0x80)+buf.getvalue())) for i in range(10)]
        for order, (start, end, track, value) in enumerate(cues):
            if start//1000 != second:
                continue
            if ass:
                value = f'{order},0,Default,,0,0,0,,{{\\pos(600,20)\\c&H0000FF&}}' + value
            block = bytes([0x80 | track])+struct.pack('>hB', start-second*1000, 0)+value.encode()
            entries.append((start, track, elem('a0', elem('a1', block)+uint('9b', end-start))))
        cluster_position = len(segment_prefix)+sum(map(len, clusters))
        body = uint('e7', second*1000)
        for stamp, track, block in sorted(entries):
            # A real seek must redeliver subtitle cues spanning its target.
            # CueDuration and CueRelativePosition let the native MKV splitter
            # retrieve those packets without scanning from the start.
            if track != 1 or stamp == second*1000:
                positions = uint('f7', track)+uint('f1', cluster_position)+uint('f0', len(body))
                if track != 1:
                    duration = next(end-start for start, end, number, _ in cues if start == stamp and number == track)
                    positions += uint('b2', duration)
                index.append((stamp, track, elem('bb', uint('b3', stamp)+elem('b7', positions))))
            body += block
        clusters.append(elem('1f43b675', body))
    cue_index = elem('1c53bb6b', b''.join(x[2] for x in sorted(index)))
    segment_prefix = seek_head(len(segment_prefix)+sum(map(len, clusters)))+segment_prefix[len(placeholder):]
    path.write_bytes(header+elem('18538067', segment_prefix+b''.join(clusters)+cue_index))


def select(dlg, slot, index):
    control = user.GetDlgItem(dlg, 616+slot)
    assert query(control, 0x14e, index) == index
    user.PostMessageW(dlg, 0x111, (1 << 16) | (616+slot), control)
    time.sleep(.15)


def both(port, folder, name):
    counts, h = picture(port, folder, name)
    assert sum(counts[int(h*.64):int(h*.81)]) > 30, ('primary missing', name)
    assert sum(counts[int(h*.81):]) > 30, ('secondary missing', name)


def run(renderer, ass=False):
    folder = ROOT/'_bin/bridge/embedded-qa'/f'vr{renderer}-ass{ass}-{time.time_ns()}'
    folder.mkdir(parents=True)
    movie = folder/'two-tracks.mkv'
    mkv(movie, ass)
    external = folder/'external.srt'
    external.write_text('1\n00:00:01,000 --> 00:00:09,000\nExternal translation\n', encoding='utf-8-sig')
    player = folder/'embedded-player.exe'
    shutil.copy2(ROOT/'_bin/mpc-be_x64/mpc-be64.exe', player)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    player.with_suffix('.ini').write_text(f'[Audio]\nMute=1\n[Video]\nVideoRenderer={renderer}\n[Settings]\nSnapShotSubtitles=1\n[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nEnablePreview=1\nPort={port}\n', encoding='utf-16')
    process = subprocess.Popen([str(player), str(movie), '/new', '/minimized'])
    hwnd = None
    try:
        wait(port, lambda s: int(s.get('position', '0')) > 500)
        hwnd = window(process.pid)
        assert hwnd
        user.ShowWindow(ctypes.c_void_p(hwnd), 9)
        time.sleep(1)
        user.SetWindowPos(ctypes.c_void_p(hwnd), ctypes.c_void_p(-1), 100, 100, 800, 600, 0x0040)
        user.SetForegroundWindow(ctypes.c_void_p(hwnd))
        time.sleep(1)
        user.PostMessageW(hwnd, 0x111, 888, 0)
        wait(port, lambda s: s['state'] == '1')
        dlg = dialog(hwnd)
        count = query(user.GetDlgItem(dlg, 616), 0x146)
        assert count == 3, ('both embedded tracks must be listed', count)
        select(dlg, 0, 1)
        select(dlg, 1, 2)
        click(dlg, 614)
        assert user.GetLastActivePopup(hwnd) == dlg, 'embedded apply failed'
        click(dlg, 2)
        seek(port, 3000)
        both(port, folder, 'two-embedded')
        seek(port, 1500)
        counts, h = picture(port, folder, 'before-secondary')
        assert sum(counts[int(h*.81):]) < 20
        seek(port, 11000)
        counts, _ = picture(port, folder, 'gap')
        assert sum(counts) < 30, 'streaming seek kept stale cues'
        seek(port, 16000)
        counts, _ = picture(port, folder, 'later-packet')
        assert sum(counts[int(h*.64):int(h*.81)]) > 30, 'new segment packets were not refreshed'
        seek(port, 3000)
        both(port, folder, 'seek-back')
        if '--stress' in sys.argv:
            import psutil
            native = psutil.Process(process.pid)
            before = native.memory_info().rss
            dlg = dialog(hwnd)
            for number in range(100):
                edit(dlg, 607, number % 2 * 500)
                click(dlg, 614)
                assert user.GetLastActivePopup(hwnd) == dlg
            click(dlg, 2)
            delta = native.memory_info().rss-before
            assert delta < 64*1024*1024, ('embedded working set growth', delta)
            print(f'PASS: 100 embedded replacements; RSS delta {delta} bytes', flush=True)
        dlg = dialog(hwnd)
        edit(dlg, 607, 2000)
        click(dlg, 614)
        click(dlg, 2)
        seek(port, 3000)
        counts, _ = picture(port, folder, 'secondary-delay')
        assert sum(counts[int(h*.81):]) < 20
        dlg = dialog(hwnd)
        edit(dlg, 607, 0)
        select(dlg, 0, 0)
        edit(dlg, 600, external)
        click(dlg, 614)
        assert user.GetLastActivePopup(hwnd) == dlg
        click(dlg, 2)
        seek(port, 3000)
        both(port, folder, 'file-and-embedded')
        dlg = dialog(hwnd)
        select(dlg, 0, 1)
        select(dlg, 1, 0)
        edit(dlg, 601, external)
        click(dlg, 614)
        assert user.GetLastActivePopup(hwnd) == dlg
        click(dlg, 2)
        seek(port, 3000)
        both(port, folder, 'embedded-and-file')
        dlg = dialog(hwnd)
        select(dlg, 1, 1)
        click(dlg, 614)
        popup = user.GetLastActivePopup(hwnd)
        assert popup and popup != dlg, 'duplicate embedded track accepted'
        user.PostMessageW(popup, 0x10, 0, 0)
        time.sleep(.2)
        click(dlg, 2)
        both(port, folder, 'duplicate-preserves-previous')
        user.PostMessageW(hwnd, 0x111, 1171, 0)
        time.sleep(.5)
        counts, _ = picture(port, folder, 'hidden')
        assert sum(counts) < 30
        user.PostMessageW(hwnd, 0x111, 1171, 0)
        time.sleep(.5)
        both(port, folder, 'shown')
        dlg = dialog(hwnd)
        click(dlg, 613)
        click(dlg, 614)
        click(dlg, 2)
        seek(port, 3000)
        both(port, folder, 'swapped')
        user.PostMessageW(hwnd, 0x111, 830, 0)
        time.sleep(.5)
        user.PostMessageW(hwnd, 0x111, 891, 0)
        user.PostMessageW(hwnd, 0x111, 830, 0)
        time.sleep(.5)
        seek(port, 3000)
        both(port, folder, 'fullscreen-frame-step')
        dlg = dialog(hwnd)
        click(dlg, 615)
        print(f'PASS renderer {renderer}, ASS={ass}: two embedded tracks, both file mixes, packet updates, seeks/gaps, delays, duplicate rollback, toggle, swap, fullscreen; {folder}', flush=True)
    finally:
        if hwnd:
            user.PostMessageW(hwnd, 0x111, 816, 0)
        try:
            process.wait(10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise AssertionError('embedded subtitle close hung')
    assert process.returncode == 0, process.returncode


if __name__ == '__main__':
    for renderer in ([int(sys.argv[1])] if len(sys.argv)>1 and sys.argv[1].isdigit() else [3, 5]):
        run(renderer, '--ass' in sys.argv)
