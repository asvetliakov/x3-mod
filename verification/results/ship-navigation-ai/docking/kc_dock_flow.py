#!/usr/bin/env python3
"""Pseudo-code of the KC docking methods quoted in docs/reverse-engineering/ship-navigation-ai.md section 11:
2004::__LandInHangar, __StartInHangar, SafeUndock (Mayhem only), CollisionWarn, __Idle and GetFormationSize, for stock
AP (addon/04.cat) and Mayhem 4 (addon/07.cat), with a stock-vs-Mayhem equality line per method.

Reuses the loader and symbolic stack tracker of ../gates/kc_gate_flow.py (definitions up to METHODS).

    python3 -I verification/results/ship-navigation-ai/docking/kc_dock_flow.py [X3 dir] > kc_dock_flow.txt
"""
import re, sys
from pathlib import Path
gf = Path(__file__).resolve().parent.parent / 'gates' / 'kc_gate_flow.py'
src = gf.read_text()
g = {'__file__': str(gf), '__name__': 'kc_gate_flow_defs'}
exec(compile(src[:src.index('\nMETHODS = ')], 'kc_gate_flow.py', 'exec'), g)
Obj, member = g['Obj'], g['member']
X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
METHODS = [(2004, n) for n in ('__LandInHangar', '__StartInHangar', 'SafeUndock', 'CollisionWarn', '__Idle',
                               'GetFormationSize')]
norm = lambda s: re.sub(r'\b[mgp]-?\d+\b', 'X', re.sub(r'goto [0-9a-f]+', 'goto', re.sub(r'\b[0-9a-f]{5}\b', 'OFF', s)))
texts = {}
for cat, label in ((X3 / 'addon/04.cat', 'stock AP'), (X3 / 'addon/07.cat', 'Mayhem 4')):
    o = Obj(member(cat, 'L/x3story.obj'))
    print('== %s L/x3story.obj (%s): CODE %d B' % (cat.relative_to(X3), label, len(o.code)))
    for cid, nm in METHODS:
        found = o.find(cid, nm)
        if not found: print('\n-- %d::%s not found' % (cid, nm))
        for e in found:
            lines, bad = o.track(e)
            print('\n-- %d::%s entry %05x, depth conflicts %d' % (cid, nm, e, bad))
            for pc, dep, line in lines: print('  %05x d%-3d %s' % (pc, dep, line))
            texts.setdefault(label, {})[(cid, nm)] = [norm(l) for _, _, l in lines]
    print()
for key, a in texts['stock AP'].items():
    b = texts['Mayhem 4'].get(key)
    print('%d::%s stock vs Mayhem: statements equal after masking slots and offsets: %s (%d vs %s statements)'
          % (key[0], key[1], a == b, len(a), len(b) if b else '-'))
