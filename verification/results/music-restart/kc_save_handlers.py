#!/usr/bin/env python3
"""Story-script side of music-restart.md section 7: what BeforeSave/SaveFinished/SaveFinished2 (class 150, the
objects the main loop calls at 0x00403df0/0x00403ec1/0x00403eed) do, which MOV_ natives they reach through
their call closure, and the flags the music player 200::PlayMusicWithFade passes to MOV_LoadMovie.

Reads L/x3story.obj in memory from addon/04.cat (stock AP) and addon/07.cat (Mayhem 4) with the gate-queue
decoder (verification/results/ship-navigation-ai/gate-queue/kc_obj.py). Prints decoded statements only (no
raw bytes); nothing is written.

    python3 -I verification/results/music-restart/kc_save_handlers.py [X3 dir] > kc_save_handlers.txt
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ship-navigation-ai' / 'gate-queue'))
from kc_obj import Obj, load_member  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
BUILDS = [('stock AP', X3 / 'addon/04.cat'), ('Mayhem 4', X3 / 'addon/07.cat')]
HANDLERS = ['BeforeSave', 'SaveFinished', 'SaveFinished2']


def closure(o, entry, limit=400):
    """Methods reachable through callcls/callself (0x86/0x88) and the MOV_ natives met on the way."""
    seen, work, movs = set(), [entry], []
    while work and len(seen) < limit:
        e = work.pop()
        if e in seen:
            continue
        seen.add(e)
        for pc in o.insns(e):
            op = o.code[pc]
            if op in (0x86, 0x88):
                v = struct.unpack_from('>I', o.code, pc + 1)[0]
                if v in o.meth:
                    work.append(v)
            elif op == 0x82 and o.operand(pc).startswith('MOV_'):
                movs.append(('%d::%s' % o.meth[e][0][:2], o.operand(pc)))
    return seen, sorted(set(movs))


for label, cat in BUILDS:
    o = Obj(load_member(cat))
    print('== %s (build id %d)' % (label, o.build))
    for nm in HANDLERS:
        for e in o.find(150, nm):
            lines, bad = o.track(e)
            print('150::%s @%05x (stack conflicts %d)' % (nm, e, bad))
            for pc, depth, line in lines:
                if line.startswith(('call', 'if', 'return', 'goto')):
                    print('   ', line)
            seen, movs = closure(o, e)
            print('    closure: %d methods; MOV_ natives reached: %s' % (len(seen), movs or 'none'))
    for e in o.find(200, 'PlayMusicWithFade'):
        lines, bad = o.track(e)
        loads = [line for _, _, line in lines if 'MOV_LoadMovie' in line or 'MOV_PlayMovie' in line]
        print('200::PlayMusicWithFade @%05x (stack conflicts %d): %s' % (e, bad, '; '.join(sorted(set(loads)))))
