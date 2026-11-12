#!/usr/bin/env python3
"""The lone bullet draw's render states (docs/reverse-engineering/fl/bullet-draw-batch.md section 7): derived only.

The lone path draws an effects-material bullet body with effects.fx DEFAULT (the material submit, section 7). Prints
(1) the pass-0 render states of `shader/2_0/effects.fb` DEFAULT as D3DX applies them: a literal's value, or for an
FXLC expression the one parameter it reads (every expression of this pass is a single `mov` of one parameter) and
that parameter's effect default; states the pass does not set (SRGBWriteEnable) are left as the device holds them;
(2) per TBullets row whose body has an effects.fx material, the material's g_* overrides (bob1 material params), so
the state the game applies is the override, else the effect default. Numbers and names only; writes nothing.

  python3 -I verification/results/projectile-reshade/lone_states.py [--game /mnt/c/X3FL] \
    > verification/results/projectile-reshade/lone_states_out.txt
"""
import argparse
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'tools/effects'))
from effect_passes import _archive_effects, parse_container, STATE_TABLE, _state_kind, _u32  # noqa: E402
import weapon_effects_census as census  # noqa: E402
from sector_fog_census import Assets  # noqa: E402
import bob1  # noqa: E402

KEYS = ('g_zenable', 'g_zwriteenable', 'g_alphatestenable', 'g_alphablendenable', 'g_srcblend', 'g_destblend',
        'g_blendop', 'g_cullmode', 'g_alphavalue', 'g_enablefog')


def pass_states(game):
    for _, path, data in _archive_effects(game, edition='fl'):
        if path != 'shader/2_0/effects.fb':
            continue
        c = parse_container(data)
        base = c['base']
        defaults = {}
        for p in c['parameters']:
            if p['class'] == 0 and p['type'] in (1, 2, 3) and p['name'].startswith('g_'):
                (v,) = _u32(data, base + p['value_offset'])
                defaults[p['name']] = struct.unpack('<f', struct.pack('<I', v))[0] if p['type'] == 3 else v
        for ti, (tech, passes) in enumerate(c['techniques']):
            if tech != 'DEFAULT':
                continue
            for si, (op, _, _, value) in enumerate(passes[0][1]):
                name = STATE_TABLE.get(op, ('op_%d' % op,))[0]
                key = (ti, 0, 0xffffffff, si)
                if key not in c['resources']:
                    key = (ti, 0, 0, si)
                kind, _ = _state_kind(data, c, key, op)
                if kind == 'literal':
                    print(f'{path} DEFAULT P0 {name} = {_u32(data, base + value)[0]} (literal)')
                elif kind == 'expression':
                    _, start, size = c['resources'][key]
                    blob = data[start:start + size]
                    reads = sorted({n.decode() for n in re.findall(rb'g_[A-Za-z0-9_]+', blob)})
                    shown = ', '.join(f'{r} default {defaults.get(r)}' for r in reads)
                    print(f'{path} DEFAULT P0 {name} = expression of {shown}')
                elif op not in (146, 147):
                    print(f'{path} DEFAULT P0 {name} = {kind}')
            print(f'{path} DEFAULT P0 sets SRGBWriteEnable: '
                  f'{any(STATE_TABLE.get(s[0], ("",))[0] == "SRGBWriteEnable" for s in passes[0][1])}')


def bodies(game):
    assets = Assets(pathlib.Path(game), edition='fl')
    read = census.assets_reader(assets)
    data, _ = assets.get('types/TBullets.txt')
    seen = {}
    for w in census.parse_tbullets(data.decode('latin1')):
        name = census.element_body_name(w['body']) or w['body']
        key = bob1.body_stem(name).lower()
        if key not in seen:
            try:
                raw, _ = read(name)
                mats = bob1.materials(bob1.parse(raw))
            except (FileNotFoundError, bob1.FormatError):
                mats = None
            rows = []
            for m in mats or []:
                if 'params' not in m or not census._str(m['effect']).lower().startswith('effects'):
                    continue
                p = {census._str(n).lower(): v[0] if isinstance(v, (list, tuple)) and v else v
                     for n, _, v in m['params']}
                rows.append(' '.join(f'{k}={p[k]}' for k in KEYS if k in p) or 'no g_ overrides')
            seen[key] = (name, rows, [])
        seen[key][2].append(census._str(w['weapon_id']))
    effects_bodies = [(k, v) for k, v in seen.items() if v[1]]
    print(f'TBullets bodies {len(seen)}, with an effects.fx material {len(effects_bodies)}')
    for _, (name, rows, ids) in effects_bodies:
        for r in rows:
            print(f'{name} | {r} | {",".join(ids)}')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--game', default='/mnt/c/X3FL')
    args = ap.parse_args()
    pass_states(args.game)
    bodies(args.game)


if __name__ == '__main__':
    main()
