"""Opt-in local-movie regression; reads user-specified media without changing it.

Private paths, source names and small screenshots stay in ignored _bin/bridge.
Controls only a copied player process and its temporary INI, never other players.
"""
import argparse
import ctypes
import io
import json
import pathlib
import shutil
import socket
import subprocess
import time
from PIL import Image, ImageChops
from dual_subtitle_playback import ROOT, dialog, click, edit, seek, request
from embedded_subtitle_playback import query, select
from quality_cache_playback import user, window, wait


def labels(dlg):
    combo = user.GetDlgItem(dlg, 616)
    items = []
    for i in range(query(combo, 0x146)):
        length = query(combo, 0x149, i)
        assert length < 32768
        text = ctypes.create_unicode_buffer(length + 1)
        result = ctypes.c_size_t()
        assert user.SendMessageTimeoutW(combo, 0x148, i, ctypes.addressof(text), 2, 3000, ctypes.byref(result))
        items.append(text.value)
    return items


def control_text(dlg, identifier):
    control = user.GetDlgItem(dlg, identifier)
    assert control
    text = ctypes.create_unicode_buffer(2048)
    result = ctypes.c_size_t()
    assert user.SendMessageTimeoutW(control, 0x000d, len(text), ctypes.addressof(text), 2, 3000, ctypes.byref(result))
    return text.value


def run(args):
    movie = pathlib.Path(args.movie).resolve(strict=True)
    folder = ROOT/'_bin/bridge/real-subtitle-qa'/f'vr{args.renderer}-{time.time_ns()}'
    folder.mkdir(parents=True)
    player = folder/'real-subtitle-player.exe'
    shutil.copy2(ROOT/'_bin/mpc-be_x64/mpc-be64.exe', player)
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    player.with_suffix('.ini').write_text(
        f'[Audio]\nMute=1\n[Video]\nVideoRenderer={args.renderer}\n[Settings]\nSnapShotSubtitles=1\n'
        f'[WebServer]\nEnableWebServer=1\nLocalhostOnly=1\nEnablePreview=1\nPort={port}\n', encoding='utf-16')
    process = subprocess.Popen([str(player), str(movie), '/new', '/minimized'])
    hwnd = None
    try:
        wait(port, lambda s: int(s.get('position', '0')) > 500, timeout=45)
        hwnd = window(process.pid)
        assert hwnd
        user.ShowWindow(ctypes.c_void_p(hwnd), 9)
        user.SetWindowPos(ctypes.c_void_p(hwnd), ctypes.c_void_p(-1), 100, 100, 1000, 700, 0x0040)
        time.sleep(1)
        user.PostMessageW(hwnd, 0x111, 888, 0)
        wait(port, lambda s: s['state'] == '1')
        seek(port, args.at[0])
        dlg = dialog(hwnd)
        names = labels(dlg)
        (folder/'source-catalog.json').write_text(json.dumps(names, ensure_ascii=False, indent=2), encoding='utf-8')
        if args.expect is not None:
            assert len(names)-1 == args.expect, names
        chosen = []
        for hint in (args.primary, args.secondary):
            if hint is None:
                chosen.append(0)
                continue
            matches = [i for i, name in enumerate(names) if i > 0 and hint.casefold() in name.casefold()]
            assert len(matches) == 1, (hint, matches, names)
            chosen.append(matches[0])
        assert chosen[0] != chosen[1]
        select(dlg, 0, chosen[0])
        select(dlg, 1, chosen[1])
        if args.secondary_file:
            edit(dlg, 601, pathlib.Path(args.secondary_file).resolve(strict=True))
        click(dlg, 614)
        assert user.GetLastActivePopup(hwnd) == dlg, 'applying the pair displayed an error'
        if args.close_while_reading:
            status = control_text(dlg, 620)
            assert 'reading' in status.casefold() or '读取' in status, ('no active background read', status)
        if args.wait_ready:
            end = time.monotonic()+args.wait_ready
            last = ''
            while time.monotonic() < end:
                status = control_text(dlg, 620)
                if status != last:
                    print(status, flush=True)
                    last = status
                assert 'failed' not in status.casefold() and '失败' not in status, status
                if 'ready' in status.casefold() or '已就绪' in status:
                    break
                time.sleep(.5)
            else:
                raise AssertionError(('full subtitle preparation timed out', last))
            # The dialog reports worker completion; allow the UI's periodic
            # publication to replace the live pin providers atomically.
            time.sleep(1)
        click(dlg, 2)

        def snapshot(name):
            data = request(port, '/snapshot.jpg')
            (folder/(name+'.jpg')).write_bytes(data)
            return Image.open(io.BytesIO(data)).convert('RGB')

        results = []
        # Visit the last point first, then return to the first: the native
        # decoder must repopulate cues after both forward and backward seeks.
        visits = [] if args.close_while_reading else [(at, True) for at in args.at]
        if not args.close_while_reading:
            visits += [(at, False) for at in args.gap]
        if len(visits) > 1:
            visits.append((args.at[0], True))
        for visit, (at, expected) in enumerate(visits):
            seek(port, at)
            video_deadline = time.monotonic()+5
            # MPCVR's paused swap chain may need several redraws. Capture a
            # stable state before comparing the same paused movie frame.
            time.sleep(1)
            visible = snapshot(f'{visit}-dual-visible')
            user.PostMessageW(hwnd, 0x111, 1171, 0)
            time.sleep(1)
            hidden = snapshot(f'{visit}-subtitles-hidden')
            user.PostMessageW(hwnd, 0x111, 1171, 0)
            time.sleep(1)
            restored = snapshot(f'{visit}-dual-restored')
            w, h = visible.size
            # A 4K paused seek can report its position before a new decoded
            # video frame reaches MPCVR's swap chain. Do not mistake a whole
            # previous movie frame for a subtitle ghost. Require the picture
            # above both lanes to match the hidden reference before measuring.
            video_retries = 0
            while True:
                background = ImageChops.difference(visible, hidden).crop((0, 0, w, int(h*.6)))
                mismatches = sum(max(p) > 24 for p in background.getdata())
                if mismatches < w*h*.006:
                    break
                assert time.monotonic() < video_deadline, ('paused video frame did not settle', at, mismatches)
                visible = snapshot(f'{visit}-video-settled')
                video_retries += 1
                time.sleep(.1)

            def changed(image, top, bottom):
                diff = ImageChops.difference(image, hidden).crop((0, int(h*top), w, int(h*bottom)))
                return sum(max(p) > 24 for p in diff.getdata())

            counts = [changed(visible, .64, .81), changed(visible, .81, 1)]
            restored_counts = [changed(restored, .64, .81), changed(restored, .81, 1)]
            sample = {'at_ms': at, 'expected_visible': expected, 'changed_pixels': counts,
                      'restored_changed_pixels': restored_counts, 'video_frame_retries': video_retries}
            results.append(sample)
            (folder/'result.json').write_text(json.dumps({'renderer': args.renderer,
                'source_count': len(names)-1, 'selected': [names[i] for i in chosen],
                'secondary_file': bool(args.secondary_file), 'samples': results}, indent=2), encoding='utf-8')
            if expected:
                assert min(counts) > 30 and min(restored_counts) > 30, sample
            else:
                assert max(counts + restored_counts) < 30, sample
        if args.close_while_reading:
            print('Closing the isolated player while its subtitle read is active.', flush=True)
        else:
            print(f'PASS: {len(names)-1} real embedded tracks, selected pair, {len(results)} seek/gap and hide/restore checks; {folder}', flush=True)
    finally:
        if hwnd:
            user.PostMessageW(hwnd, 0x111, 816, 0)
        try:
            process.wait(15)
        except subprocess.TimeoutExpired:
            debugger = pathlib.Path('C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe')
            if debugger.exists():
                # Inspect only this test's child before terminating it. Keep
                # a close failure diagnosable instead of hiding it in a retry.
                with (folder/'close-hang-stacks.txt').open('w', encoding='utf-8') as log:
                    try:
                        subprocess.run([str(debugger), '-p', str(process.pid), '-y',
                            str(ROOT/'_bin/mpc-be_x64'), '-c', '~* kb; qd'],
                            stdout=log, stderr=subprocess.STDOUT, timeout=30)
                    except subprocess.TimeoutExpired:
                        pass
            process.kill()
            process.wait()
            raise AssertionError('private-media test player did not close')
    assert process.returncode == 0, process.returncode
    if args.close_while_reading:
        print('PASS: cancellation during background read and normal process exit', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--movie', required=True)
    parser.add_argument('--primary', required=True, help='Unique substring of the source label')
    secondary = parser.add_mutually_exclusive_group(required=True)
    secondary.add_argument('--secondary', help='Unique substring of the source label')
    secondary.add_argument('--secondary-file', help='Read-only external text subtitle')
    parser.add_argument('--at', required=True, action='append', type=int,
                        help='Confirmed overlapping dialogue time in milliseconds; repeat for multiple seeks')
    parser.add_argument('--gap', default=[], action='append', type=int,
                        help='Confirmed blank time in BOTH selected sources; repeat for multiple gaps')
    parser.add_argument('--expect', type=int, help='Expected embedded-source count')
    parser.add_argument('--renderer', type=int, choices=(3, 5), default=3)
    preparation = parser.add_mutually_exclusive_group()
    preparation.add_argument('--wait-ready', type=int, default=0,
                        help='Seconds to wait for independently prepared complete sources; opt-in')
    preparation.add_argument('--close-while-reading', action='store_true',
                             help='Check cancellation and normal exit during full-track preparation')
    run(parser.parse_args())
