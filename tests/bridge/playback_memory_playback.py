"""Cross-process playback memory regression using isolated INI and owned processes.

The local MJPEG/SRT fixtures verify native seeking and rendered subtitle state.
The HTTP/extractor fixture verifies webpage identity and quality restoration.
"""
import ctypes
import configparser
import json
import pathlib
import shutil
import socket
import subprocess
import sys
import threading
import time
from http.server import ThreadingHTTPServer
from dual_subtitle_playback import video, dialog, edit, click, seek, request
from quality_cache_playback import ROOT, Files, user, window, wait, quality_menu


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def edit_value(dlg, control):
    value = ctypes.create_unicode_buffer(2048)
    result = ctypes.c_size_t()
    assert user.SendMessageTimeoutW(user.GetDlgItem(dlg, control), 0x000d, 2048,
                                    ctypes.addressof(value), 2, 3000, ctypes.byref(result))
    return value.value


class Player:
    def __init__(self, folder, online=False):
        folder.mkdir(parents=True)
        self.folder = folder
        self.exe = folder / 'memory-player.exe'
        shutil.copy2(ROOT / '_bin/mpc-be_x64/mpc-be64.exe', self.exe)
        self.port = free_port()
        profile = dict(schemaVersion=2, id='memory-test', displayName='Memory test', bindings=[
            dict(key='R', action='rate.relative', args=dict(step=.5)),
            dict(key='Right', action='seek.relative', args=dict(seconds=5),
                 hold=dict(action='rate.temporary', args=dict(rate=3), delayMs=300))])
        self.exe.with_suffix('.ini').write_text(
            '[MPC-Bridge]\nShortcutProfile=custom\nCustomProfile=' + json.dumps(profile, separators=(',', ':')) +
            f'\n[Audio]\nMute=1\n[Video]\nVideoRenderer=3\n[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nPort={self.port}\n' +
            (f'[OnlineServices]\nYDLEnable=1\nYDLExePath={folder / "fixture-parser.exe"}\nYDLVideoCodec=H264\nYDLAudioCodec=AAC\nYDLMaxHeight=1080\n' if online else ''), encoding='utf-16')
        self.process = None
        self.hwnd = None

    def start(self, source, *args):
        self.process = subprocess.Popen([str(self.exe), str(source), '/new', '/minimized', *args])
        self.hwnd = None
        current = wait(self.port, lambda s: int(s.get('duration', 0)) > 0 and s.get('state') == '2',
                       timeout=60 if str(source).startswith('http') else 20)
        self.hwnd = window(self.process.pid)
        assert self.hwnd
        self.command(888)
        return wait(self.port, lambda s: s['state'] == '1')

    def settings(self, **values):
        config = configparser.RawConfigParser(strict=False)
        config.optionxform = str
        path = self.exe.with_suffix('.ini')
        data = path.read_bytes()
        config.read_string(data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))
        if not config.has_section('Settings'):
            config.add_section('Settings')
        for key, value in values.items():
            config.set('Settings', key, str(value))
        with path.open('w', encoding='utf-16') as file:
            config.write(file, space_around_delimiters=False)

    def command(self, command):
        user.PostMessageW(self.hwnd, 0x111, command, 0)

    def key(self, key, down=True):
        user.PostMessageW(self.hwnd, 0x100 if down else 0x101, ord(key) if len(key) == 1 else 0x27,
                          1 if down else -1073741823)

    def close(self, crash=False):
        if self.process and self.process.poll() is None:
            self.hwnd = window(self.process.pid)
            if crash:
                self.process.kill()
            else:
                popup = user.GetLastActivePopup(self.hwnd) if self.hwnd else None
                if popup and popup != self.hwnd:
                    user.PostMessageW(popup, 0x111, 2, 0)
                user.PostMessageW(self.hwnd, 0x10, 0, 0)
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()
                raise AssertionError('owned player did not exit')
        if self.process and not crash:
            assert self.process.returncode == 0, ('player exited abnormally', self.process.returncode)
        self.process = None

    def records(self):
        return json.loads((self.folder / 'playback-memory.json').read_text(encoding='utf-8'))['records']

    def saved(self, condition, timeout=8):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                records = self.records()
                if condition(records):
                    return records
            except (OSError, ValueError):
                pass
            time.sleep(.1)
        raise AssertionError(('record not saved', self.records()))


def verify_dual(player, primary, secondary):
    dlg = dialog(player.hwnd)
    for control, expected in ((600, str(primary)), (601, str(secondary)), (606, '500'), (607, '-500')):
        value = edit_value(dlg, control)
        assert value == expected, (control, value, expected)
    for control, expected in ((608, '24'), (609, '18'), (610, '#F0F0F0'), (611, '#FFCC00')):
        assert edit_value(dlg, control) == expected, (control, edit_value(dlg, control), expected)
    result = ctypes.c_size_t()
    assert user.SendMessageTimeoutW(user.GetDlgItem(dlg, 612), 0x0147, 0, 0, 2, 3000, ctypes.byref(result))
    assert result.value == 1 and not checked(dlg, 605), 'subtitle layout/visibility was not restored'
    click(dlg, 2)


def local_checks(folder):
    p = Player(folder)
    movie = folder / '视频 A.avi'
    other = folder / '视频 B.avi'
    video(movie)
    video(other)
    primary, secondary = folder / '原文 a.srt', folder / '译文 b.srt'
    for path, word in ((primary, 'PRIMARY'), (secondary, 'SECONDARY')):
        path.write_text(f'1\n00:00:00,000 --> 00:00:23,000\n{word}\n', encoding='utf-8')
    ini = p.exe.with_suffix('.ini')
    ini.write_text(ini.read_text(encoding='utf-16') + '\n[MPC-Bridge Dual Subtitles]\nColor0=15790320\nColor1=52479\n', encoding='utf-16')
    try:
        p.start(movie)
        p.key('R'); p.key('R', False)
        wait(p.port, lambda s: abs(float(s['playbackrate']) - 1.5) < .01)
        dlg = dialog(p.hwnd)
        for control, value in ((600, primary), (601, secondary), (606, 500), (607, -500),
                               (608, 24), (609, 18)):
            edit(dlg, control, value)
        result = ctypes.c_size_t()
        assert user.SendMessageTimeoutW(user.GetDlgItem(dlg, 612), 0x014e, 1, 0, 2, 3000, ctypes.byref(result))
        checked(dlg, 605, False)
        click(dlg, 614); click(dlg, 2)
        seek(p.port, 10000)
        p.saved(lambda r: len(r) == 1 and abs(r[0]['positionMs'] - 10000) < 300 and r[0]['dualEnabled'])
        p.close()
        current = p.start(movie)
        assert 9800 <= int(current['position']) <= 13000, current
        assert abs(float(current['playbackrate']) - 1.5) < .01, current
        verify_dual(p, primary, secondary)
        print('PASS: close/reopen restores position, 1.5x speed and dual subtitle paths/delays/style/layout/visibility', flush=True)
        p.command(890)  # stop must preserve the point before the native reset to zero
        wait(p.port, lambda s: s['state'] == '0')
        time.sleep(.6)
        p.close()
        current = p.start(movie)
        assert int(current['position']) >= 9800, current
        print('PASS: stop does not erase the previous position', flush=True)
        p.close()
        current = p.start(other)
        assert int(current['position']) < 3000 and abs(float(current['playbackrate']) - 1) < .01, current
        dlg = dialog(p.hwnd)
        value = edit_value(dlg, 601)
        assert not value, ('another video inherited secondary subtitle', value)
        click(dlg, 2)
        p.close()
        print('PASS: different local videos keep separate progress/settings', flush=True)
        current = p.start(movie, '/start', '0')
        assert int(current['position']) < 3000, current
        print('PASS: explicit zero start overrides saved progress', flush=True)
        p.command(887)
        wait(p.port, lambda s: s['state'] == '2')
        p.key('Right')
        wait(p.port, lambda s: abs(float(s['playbackrate']) - 3) < .01)
        p.saved(lambda r: any(x['dualEnabled'] and x['positionMs'] > 5000 and x['rate'] == 1.5 for x in r))
        p.close(crash=True)
        current = p.start(movie)
        assert abs(float(current['playbackrate']) - 1.5) < .01 and int(current['position']) > 4500, current
        print('PASS: crash checkpoint retains base speed during temporary 3x hold', flush=True)
        p.command(51002)
        time.sleep(.8)
        p.close()
        current = p.start(movie)
        assert int(current['position']) < 3000, current
        print('PASS: forget video does not resurrect legacy position', flush=True)
        p.close()
        # Missing partner keeps the original pair in storage and plays the available lane.
        current = p.start(other)
        dlg = dialog(p.hwnd)
        edit(dlg, 600, primary); edit(dlg, 601, secondary); click(dlg, 614); click(dlg, 2)
        p.close()
        secondary.rename(folder / 'temporarily-missing.srt')
        p.start(other)
        seek(p.port, 8000)
        p.close()
        record = next(x for x in p.records() if x['source'].endswith('视频 b.avi'))
        assert record['dualEnabled'] and record['slots'][1]['path'] == str(secondary), record
        (folder / 'temporarily-missing.srt').rename(secondary)
        p.start(other)
        dlg = dialog(p.hwnd)
        value = edit_value(dlg, 601)
        assert value == str(secondary), value
        click(dlg, 2)
        print('PASS: unavailable subtitle does not overwrite saved pair', flush=True)
        p.close()
        # A real completion event clears only position; speed/settings remain.
        p.start(other)
        seek(p.port, 22500)
        p.command(887)
        wait(p.port, lambda s: s['state'] != '2', timeout=8)
        p.close()
        current = p.start(other)
        assert int(current['position']) < 3000, current
        print('PASS: completed short video reopens from the beginning', flush=True)
    finally:
        p.close()


def online_checks(folder):
    server = ThreadingHTTPServer(('127.0.0.1', 0), Files)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    p = Player(folder, online=True)
    parser = folder / 'fixture-parser.exe'
    shutil.copy2(ROOT / '_bin/bridge/fixture-parser.exe', parser)
    formats = [dict(format_id=str(h), height=h, width=640, fps=30, vcodec='avc1.64001E', acodec='none',
                    ext='mp4', protocol='http', container='mp4_dash', tbr=350, url=base + f'/{h}.mp4') for h in (1080, 720, 480)]
    formats.append(dict(format_id='audio', vcodec='none', acodec='mp4a.40.2', ext='m4a', protocol='http', container='m4a_dash', abr=64, url=base + '/audio.m4a'))
    parser.with_suffix('.json').write_text(json.dumps(dict(id='fixture', title='Memory quality fixture', duration=554.117, formats=formats)), encoding='utf-8')
    url = 'https://www.bilibili.com/video/BV13x41117TL?p=1&share_source=test'
    try:
        p.start(url)
        quality_menu(p.hwnd)
        p.command(50201)
        wait(p.port, lambda s: s['onlineheight'] == '720' and s['state'] == '1')
        seek(p.port, 60000)
        p.key('R'); p.key('R', False)
        wait(p.port, lambda s: abs(float(s['playbackrate']) - 1.5) < .01)
        p.close()
        current = p.start('https://bilibili.com/video/BV13x41117TL/?p=01&spm_id_from=another')
        assert current['onlineheight'] == '720' and int(current['position']) >= 59000 and abs(float(current['playbackrate']) - 1.5) < .01, current
        print('PASS: equivalent webpage URL restores 720p, 60s position and 1.5x after a fresh parse', flush=True)
        p.close()
        current = p.start('https://bilibili.com/video/BV13x41117TL?p=2')
        assert current['onlineheight'] == '1080' and int(current['position']) < 3000, current
        print('PASS: Bilibili parts do not share saved position or quality', flush=True)
    finally:
        p.close()
        server.shutdown(); server.server_close()


def memory_dialog(player):
    player.command(51000)
    end = time.monotonic() + 5
    while time.monotonic() < end:
        dlg = user.GetLastActivePopup(player.hwnd)
        if dlg and dlg != player.hwnd and user.GetDlgItem(dlg, 700):
            return dlg
        time.sleep(.05)
    raise AssertionError('playback memory settings dialog unavailable')


def checked(dlg, control, enabled=None):
    result = ctypes.c_size_t()
    message = 0x00f0 if enabled is None else 0x00f1
    assert user.SendMessageTimeoutW(user.GetDlgItem(dlg, control), message, int(bool(enabled)), 0, 2, 3000, ctypes.byref(result))
    return result.value


def settings_checks(folder):
    p = Player(folder)
    movie = folder / 'settings.avi'
    video(movie)
    try:
        p.start(movie)
        seek(p.port, 9000)
        p.key('R'); p.key('R', False)
        wait(p.port, lambda s: abs(float(s['playbackrate']) - 1.5) < .01)
        p.close()
        p.start(movie)
        dlg = memory_dialog(p)
        assert checked(dlg, 701) and checked(dlg, 702), 'new installs should enable progress/settings memory'
        checked(dlg, 701, False); checked(dlg, 702, False); click(dlg, 1)
        before = p.records()[0]
        seek(p.port, 2000)
        p.close()
        current = p.start(movie)
        assert int(current['position']) < 3000 and float(current['playbackrate']) == 1, current
        p.close()
        assert p.records()[0] == before, 'disabled memory wrote to saved fields'
        p.start(movie)
        dlg = memory_dialog(p)
        assert not checked(dlg, 701) and not checked(dlg, 702), 'disabled options did not persist'
        click(dlg, 2)
        p.close()
        p.settings(RememberFilePos=1, RememberVideoSettings=1)
        current = p.start(movie)
        assert int(current['position']) >= 8800 and float(current['playbackrate']) == 1.5, current
        print('PASS: independent memory switches persist and disabling does not erase or update records', flush=True)
        p.close()
        # Explicit old INI opt-out is respected while per-video settings remain enabled.
        p.settings(RememberFilePos=0)
        current = p.start(movie)
        assert int(current['position']) < 3000 and float(current['playbackrate']) == 1.5, current
        p.close()
        p.settings(RememberFilePos=1, KeepHistory=0)
        before = (folder / 'playback-memory.json').read_bytes()
        current = p.start(movie)
        seek(p.port, 5000)
        p.close()
        assert (folder / 'playback-memory.json').read_bytes() == before, 'history master switch still wrote memory'
        print('PASS: old explicit progress opt-out and history master switch are respected', flush=True)
    finally:
        p.close()


def embedded_checks(folder):
    from embedded_subtitle_playback import mkv, select, query
    p = Player(folder)
    movie = folder / 'embedded.mkv'
    mkv(movie)
    try:
        p.start(movie)
        dlg = dialog(p.hwnd)
        select(dlg, 0, 1); select(dlg, 1, 2)
        edit(dlg, 606, 200); edit(dlg, 607, -300)
        click(dlg, 614); click(dlg, 2)
        seek(p.port, 3000)
        p.close()
        p.start(movie)
        dlg = dialog(p.hwnd)
        assert query(user.GetDlgItem(dlg, 616), 0x0147) == 1
        assert query(user.GetDlgItem(dlg, 617), 0x0147) == 2
        assert edit_value(dlg, 606) == '200' and edit_value(dlg, 607) == '-300'
        click(dlg, 2)
        print('PASS: two embedded subtitle identities and independent delays survive process restart', flush=True)
    finally:
        p.close()


def public_check(folder):
    """Optional real platform check. No account, cookies or existing settings."""
    p = Player(folder, online=True)
    shutil.copy2(ROOT / '_bin/bridge/online/yt-dlp.exe', folder / 'fixture-parser.exe')
    url = 'https://www.bilibili.com/video/BV13x41117TL'
    try:
        initial = p.start(url)
        heights = initial['onlineheights'].split(',')
        target = heights[1].strip() if len(heights) > 1 else heights[0].strip()
        quality_menu(p.hwnd)
        p.command(50200 + (1 if len(heights) > 1 else 0))
        wait(p.port, lambda s: s['state'] == '1' and s['onlineheight'] == target, timeout=60)
        seek(p.port, 60000)
        p.key('R'); p.key('R', False)
        wait(p.port, lambda s: abs(float(s['playbackrate']) - 1.5) < .01)
        p.close()
        restored = p.start(url + '?spm_id_from=memory-test')
        assert restored['onlineheight'] == target and abs(float(restored['playbackrate']) - 1.5) < .01 and int(restored['position']) >= 59000, restored
        print('PASS: public Bilibili fresh-parse progress/speed/quality restoration', target, restored['position'], flush=True)
    finally:
        p.close()


def main():
    folder = ROOT / '_bin/bridge/playback-memory-qa' / str(time.time_ns())
    if '--public' in sys.argv:
        public_check(folder / 'public')
        return
    local_checks(folder / 'local')
    online_checks(folder / 'online')
    settings_checks(folder / 'settings')
    embedded_checks(folder / 'embedded')
    print(f'Playback memory native regression passed. Evidence: {folder}', flush=True)


if __name__ == '__main__':
    main()
