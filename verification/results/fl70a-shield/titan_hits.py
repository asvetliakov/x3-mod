#!/usr/bin/env python3
"""FL 70 A (2026-11-13): where the Titan's logged shield hits land on the production lathe, Run70 vs the fix.

  python3 verification/results/fl70a-shield/titan_hits.py build LOG     # extract the hits, compile titan_hits.exe
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
      python3 verification/results/fl70a-shield/titan_hits.py run        # run it on the Windows side, summarise

The profile is the installed trimmed one (verification/results/ship-profiles/titan_profile_smoothed.json); the hits
are every logged weapon_event shield_hit of the owner with radius 545751 (the Titan, TShips row 11). Work files go to
build-fl70a-shield/ (untracked); the summary prints per 1500-frame window the contact z, the Run70 and fixed lathe
points and their distance from the projectile's line of flight.
"""
import json
import pathlib
import statistics
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
WORK = ROOT / 'build-fl70a-shield'
PROFILE = ROOT / 'verification/results/ship-profiles/titan_profile_smoothed.json'


def build(log: str) -> None:
    WORK.mkdir(exist_ok=True)
    prof = json.loads(PROFILE.read_text())
    lines = [f"{prof['z0']} {prof['z1']}"] + [' '.join(str(v) for v in s) for s in prof['slices']]
    hits = 0
    with open(log, errors='replace') as f:
        for line in f:
            if not line.startswith('weapon_event ') or 'radius=545751 ' not in line or 'role=shield_hit' not in line:
                continue
            d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
            pos, direction = d['pos'].split(','), d['dir'].split(',')
            lines.append(' '.join([d['frame'], *pos, *direction]))
            hits += 1
    (WORK / 'input.txt').write_text('\n'.join(lines) + '\n')
    src = ROOT / 'verification/results/fl70a-shield/titan_hits.cpp'
    subprocess.run(['i686-w64-mingw32-g++', '-std=c++20', '-O2', '-msse2', '-mfpmath=sse', '-mstackrealign',
                    '-mincoming-stack-boundary=2', '-I', str(ROOT / 'src'), str(src), '-o', str(WORK / 'titan_hits.exe'),
                    '-static'], check=True)
    print(f'hits={hits} input={WORK / "input.txt"} exe={WORK / "titan_hits.exe"}')


def run() -> None:
    with open(WORK / 'input.txt') as stdin:
        out = subprocess.run([str(WORK / 'titan_hits.exe')], stdin=stdin, capture_output=True, text=True, check=True).stdout
    (WORK / 'output.txt').write_text(out)
    rows = []
    for line in out.splitlines():
        if line.startswith('mesh ') or line.startswith('cost '):
            print(line)
            continue
        f = line.split()
        d = dict(kv.split('=', 1) for kv in f[2:])
        rows.append((int(f[1]), d))
    vec = lambda s: [float(v) for v in s.split(',')]
    windows = {}
    for frame, d in rows:
        windows.setdefault(frame // 1500 * 1500, []).append(d)
    print('window hits inside entered contact_z_m run70_z_m fix_z_m run70_off_line_m(med,max) fix_off_line_m(max)')
    for w, ds in windows.items():
        cz = [vec(d['contact_m'])[2] for d in ds]
        oz = [vec(d['run70_m'])[2] for d in ds]
        nz = [vec(d['fix_m'])[2] for d in ds]
        oo = [float(d['run70_off_line_m']) for d in ds]
        no = [float(d['fix_off_line_m']) for d in ds]
        print(f"{w} {len(ds)} {sum(d['inside'] == '1' for d in ds)} {sum(d['entered'] == '1' for d in ds)} "
              f"{min(cz):.0f}..{max(cz):.0f} {min(oz):.0f}..{max(oz):.0f} {min(nz):.0f}..{max(nz):.0f} "
              f"{statistics.median(oo):.0f},{max(oo):.0f} {max(no):.1f}")
    allo = [float(d['run70_off_line_m']) for _, d in rows]
    alln = [float(d['fix_off_line_m']) for _, d in rows]
    print(f"all hits={len(rows)} inside={sum(d['inside'] == '1' for _, d in rows)} far={sum(d['far'] == '1' for _, d in rows)} "
          f"entered={sum(d['entered'] == '1' for _, d in rows)} beyond_z1={sum(vec(d['contact_m'])[2] > 1001.2 for _, d in rows)} "
          f"run70_off_line_m median={statistics.median(allo):.0f} max={max(allo):.0f} "
          f"fix_off_line_m max={max(alln):.2f}")


if __name__ == '__main__':
    if sys.argv[1] == 'build':
        build(sys.argv[2])
    else:
        run()
