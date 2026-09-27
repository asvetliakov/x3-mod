#!/usr/bin/env python3
"""Section 11 of docs/architecture/taa-luminance-lock.md: (1) the smaller-residual floor and (2) the release reference.

(1) Floor sweep TAU = 2, 3, 4 codes (one constant: the larger residual must reach max(TAU, RHO * range3), the smaller TAU):
    - the y-ramp column under the resolve's exponential history at 0.85 (the implementer's yramp_ema_model.py, copied
      here with the floor as a parameter): rows that ever lock in frames 2..40;
    - the fixture scene of lock_rules_model.py, rule resid2: x / y ramp lock share, vertical strut formation frame,
      slanted line lock share and ripple energy on locked pixels;
    - the raster model (lock_share_model.py --tau T): rule resid2 lock share, energy captured, plate false locks, on the
      outpost (s 110, 147), the plant (s 65) and the spacedock (s 188).
(2) Release on the fixture strut column (surround 0.5, three of the nine 3x3 pixels at 1.5 on even phases): the 3x3 mean in
    q codes per frame; reference R at creation (frame 0, a sampled phase) or a running mean R += (mean - R) / 8; a
    surround step x0.8 or x0.6 at frame 30 (the strut unchanged); prints, per threshold, the false releases over 64 rest
    frames with no step and the kill delay (frames after the step) for each step.
Usage: python3 floor_release_model.py > floor_release_model_out.txt  (the raster part takes about 45 s)"""
import math
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import lock_rules_model as lrm  # noqa: E402


def halton(i, b):
    f, r = 1.0, 0.0
    while i:
        f /= b
        r += f * (i % b)
        i //= b
    return r


JY = [halton(n % 8 + 1, 3) - .5 for n in range(64)]
q = lambda L: max(L, 0) / (1 + max(L, 0)) * 255
code = lambda L: math.floor(q(L) + .5)
plate = lambda cy: .5 + .1 * abs(cy % 16 - 8)
ROWS = range(2, 14)


def yramp_ema(floor, frames=41):
    sample = lambda y, n: plate(y + .5 - JY[n % 8])
    hist = {y: sample(y, 0) for y in range(0, 16)}
    prev = {y: 0 for y in ROWS}
    locked = set()
    for n in range(1, frames):
        new_hist = {y: hist[y] + .15 * (sample(y, n) - hist[y]) for y in range(0, 16)}
        for y in ROWS:
            pos = y - JY[n % 8]
            base = math.floor(pos)
            f = pos - base
            e = code(sample(y, n)) - code((1 - f) * hist[base] + f * hist[base + 1])
            vals = [sample(y + d, n) for d in (-1, 0, 1)]
            tau = max(floor, .25 * (q(max(vals)) - q(min(vals))))
            if n >= 2 and e * prev[y] < 0 and max(abs(e), abs(prev[y])) >= tau and min(abs(e), abs(prev[y])) >= floor:
                locked.add(y)
            prev[y] = e
        hist = new_hist
    return sorted(locked)


def floor_rows():
    xs = list(range(258, 302))
    plate_px = [(x, y) for x in range(24, 64) for y in lrm.ROWS]
    vert = [(x, y) for x in (264, 280, 296) for y in lrm.ROWS]
    for floor in (2, 3, 4):
        print(f'floor {floor} codes: y-ramp EMA rows locking {yramp_ema(floor)}')
        for axis in ('x', 'y'):
            lane = lrm.rest_run(lambda cx, cy: lrm.plate(cx, cy, axis), list(range(24, 64)), 'resid2', tau=floor)
            print(f'  fixture {axis}-ramp (phase-mean history): lock share max {max(lrm.share(l, plate_px) for l in lane):.3f}')
        lane = lrm.rest_run(lrm.strut, xs, 'resid2', tau=floor)
        form = next((n for n in range(16) if lrm.share(lane[n], vert) >= .9), -1)
        print(f'  fixture vertical struts: form frame {form}, share frames 8..15 min {min(lrm.share(l, vert) for l in lane[8:]):.2f}')
        lane = lrm.rest_run(lambda cx, cy: lrm.strut(cx, cy, 0.2), xs, 'resid2', tau=floor)
        covered = [(x, y) for x in xs for y in lrm.ROWS
                   if any(lrm.strut(x + .5 - lrm.J[n][0], y + .5 - lrm.J[n][1], 0.2) > 1 for n in range(8))]
        sd = {p: lrm.np.std([lrm.strut(p[0] + .5 - lrm.J[n][0], p[1] + .5 - lrm.J[n][1], 0.2) for n in range(8)]) for p in covered}
        form = next((n for n in range(16) if lrm.share(lane[n], covered) >= .9), -1)
        locked = [p for p in covered if lane[15][p] > 0]
        print(f'  fixture slanted line: form frame (share >= 0.9) {form}, share at 15 {lrm.share(lane[15], covered):.2f}, '
              f'ripple energy on locked {sum(sd[p] for p in locked) / sum(sd.values()):.3f}')
        out = subprocess.run([sys.executable, str(HERE / 'lock_share_model.py'), str(HERE.parent.parent.parent),
                              'stations/others/military_outpost_middleb=110,147',
                              'stations/station_scenes/others/argon_L_solarpowerplant=65',
                              'stations/station_scenes/others/argon_spacedock=188', '--rho', '0.25', '--tau', str(floor)],
                             capture_output=True, text=True)
        (HERE / f'lock_share_model_resid_tau{floor}_out.txt').write_text(out.stdout)
        body = ''
        for line in out.stdout.splitlines():
            if line.startswith('=='):
                body = line.split()[1].split('/')[-1]
            elif line.startswith('  s '):
                size = line.split()[1].rstrip(':')
            elif line.strip().startswith('resid2'):
                print(f'  raster {body} s {size}: {line.strip()}')


def release_rows():
    surround = lambda n, step: .5 * (step if n >= 30 else 1.0)
    mean_q = lambda n, step: q((6 * surround(n, step) + 3 * (1.5 if n % 2 == 0 else surround(n, step))) / 9)
    for ref in ('creation', 'running'):
        for thr in (.6, .65, .7, .75):
            false = 0
            R = mean_q(0, 1.0)
            for n in range(1, 64):
                m = mean_q(n, 1.0)
                if min(R, m) < thr * max(R, m):
                    false += 1
                    R = m  # a release resets the reference (the lock is re-created at the next flip)
                elif ref == 'running':
                    R += (m - R) / 8
            delays = []
            for step in (.8, .6):
                R = mean_q(0, 1.0)
                if ref == 'running':
                    for n in range(1, 30):
                        R += (mean_q(n, 1.0) - R) / 8
                d = next((n - 30 for n in range(30, 64) if min(R, mean_q(n, step)) < thr * max(R, mean_q(n, step))), None)
                delays.append('survives' if d is None else f'dies at +{d}')
            print(f'release ref {ref:8s} threshold {thr:.2f}: false releases over 63 rest frames {false:2d}; step x0.8 {delays[0]}; step x0.6 {delays[1]}')


if __name__ == '__main__':
    floor_rows()
    release_rows()
