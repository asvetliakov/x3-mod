#!/usr/bin/env python3
"""Pseudo-code of the KC arrival-side methods quoted in docs/reverse-engineering/ship-navigation-ai.md section 10.10.

Reuses the loader and symbolic stack tracker of kc_gate_flow.py (its definitions only, up to METHODS) and prints the
arrival methods of class 2004 (and, with --extra, any further class::method names) for stock AP (addon/04.cat) and
Mayhem 4 (addon/07.cat), plus a stock-vs-Mayhem equality line per method after masking member/global slots.

    python3 -I verification/results/ship-navigation-ai/gates/kc_arrival_flow.py [X3 dir] > kc_arrival_flow.txt
"""
import re, sys
from pathlib import Path
src = (Path(__file__).resolve().parent / 'kc_gate_flow.py').read_text()
g = {'__file__': str(Path(__file__).resolve().parent / 'kc_gate_flow.py'), '__name__': 'kc_gate_flow_defs'}
exec(compile(src[:src.index('\nMETHODS = ')], 'kc_gate_flow.py', 'exec'), g)
Obj, member = g['Obj'], g['member']
X3 = Path(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else Path('/mnt/c/X3')
METHODS = [(2004, '__LeaveHyperspace'), (2004, '__LeaveHyperspaceWarpIn'), (2004, 'Activate'), (2004, 'AddTo2'),
           (2004, 'SetNoSafePosOnActivate'), (2014, 'GetDestinationGate')]
for a in sys.argv[1:]:
    if '::' in a: c, n = a.split('::'); METHODS.append((int(c), n))
norm = lambda s: re.sub(r'\b[mg]\d+\b', 'X', re.sub(r'goto [0-9a-f]+', 'goto', re.sub(r'\b[0-9a-f]{5}\b', 'OFF', s)))
texts = {}
for cat, label in ((X3 / 'addon/04.cat', 'stock AP'), (X3 / 'addon/07.cat', 'Mayhem 4')):
    o = Obj(member(cat, 'L/x3story.obj'))
    print('== %s L/x3story.obj (%s): CODE %d B' % (cat.relative_to(X3), label, len(o.code)))
    for cid, nm in METHODS:
        found = o.find(cid, nm)
        if not found: print('\n-- %d::%s not found' % (cid, nm))
        for e in found:
            lines, bad = o.track(e)
            print('\n-- %d::%s entry %05x args %d, depth conflicts %d' % (cid, nm, e, [m for m in o.meth[e] if m[0] == cid][0][2], bad))
            for pc, dep, line in lines: print('  %05x d%-3d %s' % (pc, dep, line))
            texts.setdefault(label, {})[(cid, nm)] = [norm(l) for _, _, l in lines]
    print()
for key, a in texts['stock AP'].items():
    b = texts['Mayhem 4'].get(key)
    print('%d::%s stock vs Mayhem: statements equal after masking member/global slots and offsets: %s (%d statements)'
          % (key[0], key[1], a == b, len(a)))
