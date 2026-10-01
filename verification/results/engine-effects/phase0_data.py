#!/usr/bin/env python3
"""Phase 0 data checks for the two call redirects in 0x00414590 (read-only, bottle X3; prints ids and counts).

Owning note: docs/reverse-engineering/engine-effects.md section 7. Views as in the engine-effects census
(`stock` = STOCK_AP_CATALOGUES, `mayhem` = installed minus the x3m LOD overlay slots).

1. types/Bodies: whether body 566 (objects/v/00566, the RCS jet) is on the JET list, the SMALLJET list or both
   (0x00434620 tests JET first, 0x00434708, then falls through to SMALLJET, 0x00434712).
2. Spawn sites per ship as 0x00414590 walks them: direct scene parts whose body is on the JET list (node
   +0x130 bit 0) with C & 0xffe == 0; redirect A is reached when also !(C & 0x4000) and TShips col 11 > 0,
   redirect B when !(C & 0x2000) and col 49 > 0. Ships without glow parts but with A-sites keep their whole
   engine look in the col-11 effect: their col-11 ids are the allowlist candidates.
3. TMissiles (class 10; loader case 0x00437ab1, file version >= 0x31: col 15 -> +0x64 effect, col 22 -> +0x84
   trail, col 26 scene): missiles whose scenes have spawn sites reach the same two call sites.
"""
import collections
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'engine-effects-census'))
import engine_effects_census as C  # noqa: E402
from sector_fog_census import Assets, STOCK_AP_CATALOGUES  # noqa: E402
import lod_overlay  # noqa: E402


def lists(assets):
    t, prov = C.text(assets, 'types/Bodies.txt')
    t = re.sub(r'//[^\n]*', '', t)
    out = {}
    for m in re.finditer(r'(SBTYPE_\w+);(\d+);(.*?)(?=SBTYPE_|\Z)', t, re.S):
        out[m.group(1)] = [x.strip() for x in m.group(3).split(';') if x.strip()]
    return out, prov


def sites(assets, scene, jets):
    try:
        data, _ = C.scene_member(assets, scene)
    except FileNotFoundError:
        return None
    if data[:4] == b'CUT1':
        return None
    out = dict(glow=0, a=0, b=0, walked=0, rcs=0)
    for p in C.scene_parts(data):
        k = C.body_key(p['body'])
        if k not in jets:
            continue
        c = p['c'] or 0
        out['walked'] += 1
        if k == 'v/00566':
            out['rcs'] += 1
        if 'emitter' not in k and k != 'v/00566':
            out['glow'] += 1
        if c & 0xffe:
            continue
        out['a'] += not c & 0x4000
        out['b'] += not c & 0x2000
    return out


def main():
    stock = Assets(C.GAME, catalogues=STOCK_AP_CATALOGUES)
    mayhem, _ = lod_overlay.original_assets(C.GAME)
    for label, a in (('stock', stock), ('mayhem', mayhem)):
        L, prov = lists(a)
        print(f'[{label}] Bodies {prov["source"]}: 566 in JET={"566" in L["SBTYPE_JET"]} '
              f'in SMALLJET={"566" in L["SBTYPE_SMALLJET"]} SMALLJET={L["SBTYPE_SMALLJET"]}')
        _, _, jets = C.jet_bodies(a)
        eff = C.effects(a)
        ships, _ = C.tships(a)
        reach = collections.Counter()
        by_id = collections.defaultdict(lambda: collections.Counter())
        for s in ships:
            st = sites(a, s['scene'], jets)
            if st is None:
                reach['scene_unread'] += 1
                continue
            e, tr = int(s['effect'] or 0), int(s['trail'] or 0)
            if st['a'] and e > 0:
                reach['ships_reaching_A'] += 1
                reach['A_calls_per_frame'] += st['a']
                by_id[e]['no_glow' if st['glow'] == 0 else 'with_glow'] += 1
            if st['b'] and tr > 0:
                reach['ships_reaching_B_if_flag'] += 1
                reach['B_calls_per_frame_if_flag'] += st['b']
            reach['max_A_sites_per_ship'] = max(reach['max_A_sites_per_ship'], st['a'])
            reach['max_walked_jets_per_ship'] = max(reach['max_walked_jets_per_ship'], st['walked'])
        print(f'  TShips rows={len(ships)} {dict(reach)}')
        for e in sorted(by_id):
            row = eff.get(e)
            els = ['%s:%s' % (el['flags'].replace('EEDF_', ''), el.get('body', el.get('prop_effect')))
                   for el in (row['elements'] if row else [])]
            print(f'  col11 {e:4d}: ships no_glow={by_id[e]["no_glow"]:3d} with_glow={by_id[e]["with_glow"]:3d} '
                  f'| {row["comment"][:44] if row else "MISSING"} | {" ".join(els)}')
        t, mprov = C.text(a, 'types/TMissiles.txt')
        lines = [x for x in t.splitlines() if x.strip() and not x.lstrip().startswith('//')]
        version, count = (int(x) for x in lines[0].split(';')[:2])
        miss = collections.Counter()
        for line in lines[1:count + 1]:
            f = line.split(';')
            st = sites(a, f[26].strip(), jets)
            if st is None:
                miss['scene_unread'] += 1
                continue
            miss['rows'] += 1
            miss['with_walked_jets'] += bool(st['walked'])
            miss['reach_A'] += bool(st['a'] and int(f[15]) > 0)
            miss['reach_B_if_flag'] += bool(st['b'] and int(f[22]) > 0)
        print(f'  TMissiles {mprov["source"]} version={version} rows={count} {dict(miss)}')


if __name__ == '__main__':
    main()
