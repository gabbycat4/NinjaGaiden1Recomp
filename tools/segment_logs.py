#!/usr/bin/env python3
"""Measure a recorded play session: run every diagnostics segment headless
with the measurement logs on, a few in parallel.

  python -I tools/segment_logs.py [REPLAY_DIR] [--jobs N] [--shot-every K]

REPLAY_DIR is a replay_diagnostic.py work folder (it holds segment-N.state and
segment-N.input); default: the newest build-play/diagnostics/replay-*.
Writes build-play/diagnostics/measure/segment-N.{scroll,ppu,oam,log} and
shots/segment-N_*.png, which Claude reads to find the train, flashes, etc.
"""
import argparse, concurrent.futures, glob, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

def last_frame(inp):
    last = 0
    for line in inp.read_text().splitlines():
        p = line.split()
        if p and p[0].isdigit():
            last = max(last, int(p[0]))
    return last

def first_frame(inp):
    for line in inp.read_text().splitlines():
        p = line.split()
        if p and p[0].isdigit():
            return int(p[0])
    return None

def run(n, rdir, out, shot_every):
    state, inp = rdir / f'segment-{n:06d}.state', rdir / f'segment-{n:06d}.input'
    # Run the whole piece: to where the next one starts (a piece with no button
    # presses, e.g. a cutscene or the ending, would otherwise run 1 frame).
    nxt = rdir / f'segment-{n + 1:06d}.input'
    start, nstart = first_frame(inp), first_frame(nxt) if nxt.exists() else None
    end = last_frame(inp) + 1
    if nstart is not None and nstart > end:
        end = nstart
    elif start is not None and nstart is None:
        end = max(end, start + 600)
    s = f'segment-{n:06d}'
    cmd = [str(ROOT / 'build-headless/ng1recomp'), str(ROOT / 'ninjagaiden.nes'),
           '--load-state', str(state), '--no-save', '--replay-exact', '--frames', str(end),
           '--input', str(inp), '--scroll-log', f'{s}.scroll', '--ppu-log', f'{s}.ppu',
           '--oam-log', f'{s}.oam', '--screenshot', f'shots/{s}.png', '--shot-every', str(shot_every)]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', NESRECOMP_REPLAY_RECORD='0')
    with open(out / f'{s}.log', 'wb') as log:
        r = subprocess.run(cmd, cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT)
    return n, r.returncode

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('replay_dir', nargs='?')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--shot-every', type=int, default=150)
    a = ap.parse_args()
    rdir = Path(a.replay_dir).resolve() if a.replay_dir else Path(sorted(glob.glob(str(ROOT / 'build-play/diagnostics/replay-*')))[-1])
    segs = sorted(int(p.name[8:14]) for p in rdir.glob('segment-*.state'))
    out = ROOT / 'build-play/diagnostics/measure'
    (out / 'shots').mkdir(parents=True, exist_ok=True)
    print(f'{len(segs)} segments from {rdir} -> {out}', flush=True)
    bad = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        for i, (n, rc) in enumerate(ex.map(lambda n: run(n, rdir, out, a.shot_every), segs), 1):
            if rc: bad.append(n)
            if i % 20 == 0 or i == len(segs): print(f'  {i}/{len(segs)} done', flush=True)
    print('all segments ran' if not bad else f'host failed on segments {bad}')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
