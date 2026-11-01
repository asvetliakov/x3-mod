#!/usr/bin/env python3
"""FL story-script transit route for the fog prefill (docs/reverse-engineering/fl/fog-prefill.md,
"Verified constants (2026-11-01)", questions 3 and 5). Decodes addon2/L/x3story.obj from the FL install with
the shared reader verification/results/ship-navigation-ai/gate-queue/kc_obj.py and prints, per method, only
the CODE offsets and names of the transit-relevant calls/natives, the SA_AllocObject(1, x) census and the
INS_CockpitSetSectorSpace / SA_SetSectorBackgroundType sites. No script bytes are written.

usage: kc_transit_route.py [--game /mnt/c/X3FL]
"""
import argparse
import bisect
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
from kc_obj import Obj, load_member  # noqa: E402

METHODS = ((2004, 'FlyThroughGate'), (2004, 'JumpToSector'), (2004, 'HyperspeedToSector'), (150, 'StartGateWarp'),
           (150, 'StartSectorWarp'), (150, 'WarpToSector'), (150, 'QuickWarp'), (150, 'EnterSector'),
           (2001, 'Activate'))
KEEP = ('Warp', 'EnterSector', 'LeaveSector', 'Activate', 'SA_AllocObject', 'SA_SetSectorBackgroundType',
        'SA_SetSpaceScene', 'Monitor', 'CleanUp', 'FreeAllBodies', 'TI_')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, default=Path('/mnt/c/X3FL'))
    args = ap.parse_args()
    o = Obj(load_member(args.game / 'addon2/03.cat', 'addon2/L/x3story.obj'))
    print(f'build {o.build} code {len(o.code)} methods {len(o.meth)}')
    for cid, nm in METHODS:
        e, = o.find(cid, nm)
        rows = [f'{pc:05x} {o.operand(pc)}' for pc in o.insns(e)
                if o.code[pc] in (0x82, 0x85, 0x86, 0x88) and any(k in o.operand(pc) for k in KEEP)]
        if (cid, nm) == (2001, 'Activate'):
            rows = [r for r in rows if int(r[:5], 16) <= 0x1076fe]
        print(f'{cid}::{nm} entry {e:05x}: ' + '; '.join(rows))
    e, = o.find(2001, 'Activate')
    print('2001::Activate SA_AllocObject args:', o.decoded(0x1075f8, 3))
    census = {'INS_CockpitSetSectorSpace': [], 'SA_SetSectorBackgroundType': [], 'SA_AllocObject main 1': []}
    total = 0
    for pc in o.all_insns():
        if o.code[pc] != 0x82:
            continue
        name = o.operand(pc)
        e = o.entries[bisect.bisect_right(o.entries, pc) - 1]
        owner = '%d::%s' % o.meth[e][0][:2]
        if name in census:
            census[name].append(f'{pc:05x} {owner}')
        if name == 'SA_AllocObject':
            total += 1
            prior = [p for p in o.insns(e) if p < pc]
            if o.decoded(prior[-2], o.width(prior[-2])) == 'push1':
                census['SA_AllocObject main 1'].append(
                    f'{pc:05x} {owner} subtype={o.decoded(prior[-3], o.width(prior[-3]))}')
    print(f'SA_AllocObject sites {total}')
    # fog-prefill.md section 2: first entry (150::Run) and the scene allocation inside 600::CreateAlloc.
    for (cid, nm), pcs in {(150, 'Run'): (0x1655f, 0x16932, 0x16947, 0x16984, 0x1666e),
                           (600, 'CreateAlloc'): (0x367d5,)}.items():
        e, = o.find(cid, nm)
        at = set(o.insns(e))
        print(f'{cid}::{nm}: ' + '; '.join(f'{pc:05x} {o.operand(pc) if pc in at else "NOT A BOUNDARY"}' for pc in pcs))
    for k, v in census.items():
        print(f'{k} {len(v)}: ' + '; '.join(v))


if __name__ == '__main__':
    main()
