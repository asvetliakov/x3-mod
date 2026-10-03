#!/usr/bin/env python3
"""After flight D: the plume floor value_eff = min(max(value, k x R), 3 x value) (engine_plumes_core.h Look::floor_ratio,
ini engine_plume_floor), R the ship's root-node radius, against run406's capture frames
(verification/results/run406-engine-plumes/draws_7144_7151_9327_20450.txt): the capital drawing xtc_red_huge (939.2)
beside xtc_red_big3 (187.5), the M6 with four xtc_red_nor3 (40), the own ship's xtc_red_nor (10) and xtc_red_tiny (5).

R estimate (offline; the proxy reads the root's +0xa4 at draw time): the engine's subtree radius 0x00488170 is
max(root +0xa0, over children and axes |offset_i| + child radius); the root's own radius is body 0's LOD value 47
(TShips column 0 = 0, objects/v/00000.pbd "testbody"); a child's radius is its LOD-0 value x max(scale x, y, z) (a jet's
x and y scale are 1, so its value at any throttle). Estimated over the scene's parts with a loadable body (positions from
the part's first frame when its flags carry bit 2, else 0; nested parts and dock cut scenes ignored). Record units =
body units x 0.01 (run406: size 939.211 for value 93922). Also the fleet's nozzle_max / R quartiles (installed and stock
views, one entry per TShips column-16 scene). Read-only on the game tree; about 90 s.

  python3 verification/results/engine-effects/floor_ratio_effects.py > verification/results/engine-effects/floor_ratio_effects_out.txt
"""
import importlib.util
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
spec = importlib.util.spec_from_file_location('engine_bodies', ROOT / 'tools/effects/engine_bodies.py')
eb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eb)
import bob1  # noqa: E402

CONTEXT = 0.01
ROOT_OWN = 47          # body 0's LOD value (objects/v/00000.pbd)
MAIN_MODE = 0x1001     # C & 0x1001 == 0x1001: a main jet the drive shows
PART = re.compile(rb'^P (\d+); B ([^;]+);([^\n]*)$', re.M)
FRAME = re.compile(rb'\{([^}]*)\}')
KS = (0.09, 0.10, 0.12, 0.15)


def tships_scenes(assets):
    text = assets.get('types/TShips.txt')[0].decode('latin1')
    lines = [l for l in text.splitlines() if l.strip() and not l.lstrip().startswith('//')]
    count = int(lines[0].split(';')[1])
    scenes = {}
    for line in lines[1:count + 1]:
        token = line.split(';')[16].strip()
        if token and token not in ('0', '-1'):
            scenes.setdefault(token.lower(), token)
    return list(scenes.values()), count


def parts_of(data):
    ps = list(PART.finditer(data))
    out = []
    for i, m in enumerate(ps):
        end = ps[i + 1].start() if i + 1 < len(ps) else len(data)
        frames = [[x.strip() for x in f.group(1).decode('latin1').split(';') if x.strip()] for f in FRAME.finditer(data[m.end():end])]
        c = re.search(rb'\bC (\d+)', m.group(3))
        pos = (0.0, 0.0, 0.0)
        if frames and frames[0]:
            try:
                if int(frames[0][0], 0) & 2 and len(frames[0]) >= 4:
                    pos = tuple(float(x) for x in frames[0][1:4])
            except ValueError:
                pass
        out.append((m.group(2).decode('latin1').strip(), int(c.group(1)) if c else 0, pos))
    return out


def body_value(assets, token, cache):
    if token in cache:
        return cache[token]
    value = None
    try:
        e = bob1.resolve_body(assets, token)
        data = assets.read_entry(e)
        try:
            lods = bob1.lods(bob1.parse(data, None))
            value = int(lods[0]['value']) if lods else None
        except bob1.FormatError:   # a text body bob1 refuses (MATERIAL3): its first record's value line
            m = re.search(rb'^\s*(\d+);\s*/\s*Automatic Object Size', data, re.M)
            value = int(m.group(1)) if m else None
    except (FileNotFoundError, bob1.BodyRefused, ValueError):
        value = None
    cache[token] = value
    return value


def scene_member(assets, token):
    stem = bob1.body_stem(token)
    for ext in ('.bod', '.bob'):
        if assets.candidates(stem + ext):
            return assets.get(stem + ext)
    raise FileNotFoundError(stem)


def ship(assets, token, jets, cache):
    """(R estimate, [(main nozzle, value)]) or None (no text scene or no main nozzle)."""
    try:
        data, _ = scene_member(assets, token)
    except FileNotFoundError:
        return None
    if data[:4] == b'CUT1':
        return None
    radius, nozzles = ROOT_OWN, []
    for body, mode, pos in parts_of(data):
        key, ident = eb.body_key(body)
        jet = jets.get(key.lower())
        if jet is not None:
            v = int(jet['value'])
        elif ident is None:
            v = body_value(assets, body, cache)
        else:
            v = None     # a numeric dummy id (camera, docking): no body radius counted
        if v is None:
            continue
        radius = max(radius, max(abs(x) for x in pos) + v)
        if jet is not None and jet['id'] != 566 and 'smalljet' not in jet['lists'] and mode & MAIN_MODE == MAIN_MODE:
            nozzles.append((key.split('\\')[-1].replace('fx_engine_xtc_red_', ''), v))
    return (radius, nozzles) if nozzles else None


def effect(k, R, v):
    return min(max(v, k * R), 3 * v)


def main():
    for view, stock in (('installed', False), ('stock', True)):
        t = eb.generate(eb.bob1.DEFAULT_GAME, stock_only=stock)
        assets, _ = eb.load_assets(eb.bob1.DEFAULT_GAME, stock)
        jets = {k.lower(): v for k, v in t['bodies'].items()}
        scenes, rows = tships_scenes(assets)
        cache, fleet = {}, {}
        for token in scenes:
            s = ship(assets, token, jets, cache)
            if s:
                fleet[token] = s
        ratios = np.array(sorted(max(v for _, v in n) / R for R, n in fleet.values()))
        q = np.percentile(ratios, [25, 40, 50, 75])
        print(f'{view}: tships_rows={rows} scenes={len(scenes)} fleet={len(fleet)} nozzle_max/R q1={q[0]:.4f} p40={q[1]:.4f} '
              f'median={q[2]:.4f} q3={q[3]:.4f} min={ratios[0]:.4f} max={ratios[-1]:.4f}')
        for k in KS:
            print(f'  k={k}: ships whose largest main nozzle is raised {int((ratios < k).sum())} of {len(ratios)}, '
                  f'to the 3x cap {int((ratios * 3 <= k).sum())}')
        if stock:
            continue
        cases = dict(capital='ships\\split\\split_m2p_ocelot_scene', m6='ships\\split\\split_m6_heavy_dragon_scene',
                     own_m4='ships\\split\\split_m4_scorpion_scene', own_ts='ships\\split\\split_ts_caiman_scene')
        for label, token in cases.items():
            match = next((s for s in fleet if s.lower() == token.lower()), None)
            if match is None:
                print(f'  {label}: {token} not in the fleet')
                continue
            R, nozzles = fleet[match]
            uniq = sorted(set(nozzles), key=lambda x: -x[1])
            for k in KS:
                cells = ', '.join(f'{n} {v * CONTEXT:g} -> {effect(k, R, v) * CONTEXT:.1f}' for n, v in uniq)
                print(f'  {label} k={k}: {match} R={R:.0f} ({R * CONTEXT:.1f} record units) kR={k * R * CONTEXT:.1f}: {cells}')


if __name__ == '__main__':
    main()
