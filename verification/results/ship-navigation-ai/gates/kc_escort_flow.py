#!/usr/bin/env python3
"""Escort switch at a gate (docs/reverse-engineering/ship-navigation-ai.md section 10.11, Q3).

1. KC (x3story.obj, stock AP addon/04.cat and Mayhem 4 addon/07.cat): pseudo-statements of 2004::__EscortObject,
   2004::__MoveToSector and 2004::__FlyToNextSector (the symbolic tracker of kc_gate_flow.py), a stock-vs-Mayhem
   equality line per method, and the argument pushes of every 702::__runScript call of __EscortObject (the four MSCI
   movement commands: escort / escort with timeout / follow with precision / follow with precision and timeout).
2. MSCI (installed loose scripts under addon/scripts): SHA-256 of the three scripts on the escort's gate path and the
   source lines (by number) that decide the switch from the 0x15 leg to order 4.

    python3 -I verification/results/ship-navigation-ai/gates/kc_escort_flow.py [X3 dir] > kc_escort_flow.txt
"""
import hashlib, html, re, struct, sys
from pathlib import Path
src = (Path(__file__).resolve().parent / 'kc_gate_flow.py').read_text()
g = {'__file__': str(Path(__file__).resolve().parent / 'kc_gate_flow.py'), '__name__': 'kc_gate_flow_defs'}
exec(compile(src[:src.index('\nMETHODS = ')], 'kc_gate_flow.py', 'exec'), g)
Obj, member = g['Obj'], g['member']
X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
METHODS = [(2004, '__EscortObject'), (2004, '__MoveToSector'), (2004, '__FlyToNextSector')]
norm = lambda s: re.sub(r'\b[mg]\d+\b', 'X', re.sub(r'goto [0-9a-f]+', 'goto', re.sub(r'\b[0-9a-f]{5}\b', 'OFF', s)))


def callsites(o, target):
    """(method, pc, pushes) for every op-0x85 call of `target`; pushes = the decoded ops of the 8 instructions before."""
    out = []
    for e in sorted(o.meth):
        prev = []
        for pc, op in o.insns(e):
            if op in (0x82, 0x85, 0x87) and o.name(struct.unpack_from('>i', o.code, pc + 1)[0]) == target:
                out.append(('%d::%s' % o.meth[e][0][:2], pc, prev[-8:]))
            w = o.width(pc)
            arg = (struct.unpack_from('>h', o.code, pc + 1)[0] if w == 3 else o.code[pc + 1] if w == 2 else
                   struct.unpack_from('>i', o.code, pc + 1)[0] if w == 5 else None)
            prev.append('%02x%s' % (op, '' if arg is None else ' %d' % arg))
    return out


texts = {}
for cat, label in ((X3 / 'addon/04.cat', 'stock AP'), (X3 / 'addon/07.cat', 'Mayhem 4')):
    o = Obj(member(cat, 'L/x3story.obj'))
    print('== %s L/x3story.obj (%s): CODE %d B' % (cat.relative_to(X3), label, len(o.code)))
    for cid, nm in METHODS:
        for e in o.find(cid, nm):
            lines, bad = o.track(e)
            print('\n-- %d::%s entry %05x args %d, depth conflicts %d' % (cid, nm, e, [m for m in o.meth[e] if m[0] == cid][0][2], bad))
            for pc, dep, line in lines: print('  %05x d%-3d %s' % (pc, dep, line))
            texts.setdefault(label, {})[(cid, nm)] = [norm(l) for _, _, l in lines]
    print('\n-- calls of __EscortObject (op 85; last 8 ops before the call: ... arg0 push, 05 argc, object push)')
    for meth, pc, pushes in callsites(o, '__EscortObject'):
        print('  %05x in %s: %s' % (pc, meth, ' | '.join(pushes)))
    print()
for key, a in texts['stock AP'].items():
    b = texts['Mayhem 4'].get(key)
    print('%d::%s stock vs Mayhem: statements equal after masking member/global slots and offsets: %s (%d statements)'
          % (key[0], key[1], a == b, len(a)))

SCRIPTS = {
    '!move.follow.template.xml': [r'^while \$leader -> exists', r'wait randomly from 2000 to 3000 ms',
                                  r'random value between 30000 and 34000', r'escort ship \$target : timeout',
                                  r'if \[SECTOR\] != \$leader\.sector', r'!move\.jump : target = \$leader'],
    '!move.jump.xml': [r'^else$', r'gosub MoveThroughGates', r'Mayhem\.Task\.Move : sector = \$sector posArrayOrTarget = \$target',
                       r'if \[OWNER\] == \[Player\] or \[OWNER\] == \[Phanon\]', r'set position'],
    'Mayhem.Task.Move.xml': [r'get next gate on (safe path|route)', r'gosub FlyThroughNextGate', r'distance between \[THIS\] and \$nextGate',
                             r'if \$distance > 10000', r'follow object \$nextGate', r'fly to sector \$nextSector',
                             r'goto label Restart'],
}
print('\n== MSCI loose scripts (installed view)')
for fn, pats in SCRIPTS.items():
    p = X3 / 'addon/scripts' / fn
    raw = p.read_bytes()
    print('\n-- %s %d B sha256 %s' % (fn, len(raw), hashlib.sha256(raw).hexdigest()))
    s = raw.decode('latin1')
    lines = re.findall(r'<line[^>]*>(.*?)</line>', s[s.find('<sourcetext'):s.find('</sourcetext>')], re.S)
    for k, l in enumerate(lines):
        t = ' '.join(html.unescape(re.sub(r'<[^>]+>', '', l)).split())
        if any(re.search(q, t) for q in pats): print('  %3d %s' % (k, t))
