"""Sequential rate regressions; requires the opt-in GPU frame probe build."""
import argparse
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]

def run(args):
    output = ROOT / '_bin/bridge/subtitle-rate-qa' / f'matrix-{time.time_ns()}'
    output.mkdir(parents=True)
    cases = [(renderer, kind, live, False)
             for renderer in (3, 5) for kind in ('text', 'ass', 'pgs')
             for live in (False, True)]
    cases += [(renderer, kind, live, True)
              for renderer in (3, 5) for kind in ('ass', 'pgs')
              for live in (False, True)]
    for index, (renderer, kind, live, boundaries) in enumerate(cases):
        label = f'vr{renderer}-{kind}-live{int(live)}-timed{int(boundaries)}'
        command = [sys.executable, str(ROOT / 'tests/bridge/subtitle_rate_playback.py'),
                   '--renderer', str(renderer), '--kind', kind,
                   '--cycles', str(1 if boundaries else args.cycles),
                   '--presenter-log', str(output / (label + '.log'))]
        if live:
            command.append('--live')
        if boundaries:
            command.append('--boundaries')
        if args.player:
            command += ['--player', str(args.player)]
        print(f'[{index+1}/{len(cases)}] {label}', flush=True)
        subprocess.run(command, cwd=ROOT, check=True)
    print(f'PASS {len(cases)} rate cases; GPU frame logs: {output}', flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cycles', type=int, default=3)
    parser.add_argument('--player', type=pathlib.Path)
    run(parser.parse_args())
