#!/usr/bin/env python3
"""KC call-site scan for the SA_ movement natives (docs/reverse-engineering/ship-navigation-ai.md).

Reads L/x3story.obj in memory from addon/04.cat (stock AP) and addon/07.cat (Mayhem 4 override, the newest
addon catalogue that carries it), XOR 0x33, STRG decode and CLAS parse per loader 0x0049d030 (as in
../native-chase-view-restore-2026-10-12/class_lookup.py). A native call is `82 <BE32 STRG offset of the name>`.
Prints, per native: site count, and each site as class id / method name / method CODE entry / site offset.
Only derived names and offsets are printed; no script bytes are written.

    python3 -I verification/results/ship-navigation-ai/kc_movement_calls.py [--all-sites] > kc_movement_calls.txt
"""
import bisect, collections, hashlib, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path('/mnt/c/X3')
NATIVES = ('SA_MovePos', 'SA_MovePosContinue', 'SA_GlidePos', 'SA_FollowObject', 'SA_FollowObjectByCut',
           'SA_EscortObject', 'SA_AttackObject', 'SA_AttackObjectBig', 'SA_DefensiveMove', 'SA_TurnToObject',
           'SA_RotateToAngles', 'SA_Idle', 'SA_StopObject', 'SA_FlyObjectIntoContainer',
           'SA_FlyObjectBeforeContainer', 'SA_FlyObjectBeforeContainerPos', 'SA_ClearObjectFromHangar',
           'SA_WarpObjectAway', 'SA_WarpObjectIn', 'SA_SetPlayerLand', 'SA_CollectDebris', 'SA_CaptureObject',
           'SA_AvoidObjectCollision', 'SA_CollisionObject', 'SA_GetCollisionObjects', 'SA_SetMaxAvoidShield',
           'SA_SetDesiredSpeed', 'SA_SetSpeed', 'SA_SetSpeedLimit', 'SA_SetMaxSpeedOverride', 'SA_SetDestination',
           'SA_SetAutopilot', 'SA_SetFlightMode', 'SA_ForceMove', 'SA_IsInApproach', 'SA_SetShipDockPortNr',
           'SA_FindNearestDockPort', 'SA_FindNearestDockPortRendezvous', 'SA_GetDockPortRendezvousPos',
           'SA_GetDestination', 'SA_GetDesiredSpeed', 'SA_SetShipRotAccel', 'SA_SetGlide')


def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))


def parse(obj):
    def sec(tag):
        a = obj.find(tag); n = struct.unpack_from('>I', obj, a + 4)[0]; return obj[a + 8:a + 8 + n]
    src = sec(b'STRG'); s = bytearray(len(src)); s[0] = ~src[0] & 255
    for i in range(1, len(src)): s[i] = (~src[i] - src[i - 1]) & 255
    s = bytes(s); c = sec(b'CLAS'); code = sec(b'CODE'); p = 0

    def w():
        nonlocal p
        v = struct.unpack_from('>I', c, p)[0]; p += 4; return v
    rows = set()
    for _ in range(w()):
        cid, parent, word, na = w(), w(), w(), w()
        ra = [(w(), w(), w(), w()) for _ in range(na)]
        rb = [(w(), w(), w(), w()) for _ in range(w())]
        for e, nm, loc, argc in ra + rb:
            rows.add((e, cid, s[nm:s.find(b'\0', nm)].decode('latin1'), argc))
    assert p == len(c), (p, len(c))
    return s, code, sorted(rows)


def scan(label, obj, all_sites):
    s, code, rows = parse(obj)
    ents = [r[0] for r in rows]
    print(f'== {label}: sha256 {hashlib.sha256(obj).hexdigest()} CODE {len(code)} B, {len(rows)} method rows')
    for nat in NATIVES:
        offs = [m.start() + 1 for m in re.finditer(b'\0' + re.escape(nat.encode()) + b'\0', s)]
        sites = []
        for off in offs:
            k = b'\x82' + struct.pack('>I', off); q = 0
            while (q := code.find(k, q)) >= 0:
                j = bisect.bisect_right(ents, q) - 1
                e, cid, nm, argc = rows[j]
                sites.append((q, cid, nm, e)); q += 1
        meth = collections.Counter((cid, nm, e) for _, cid, nm, e in sites)
        print(f'{nat:34s} {len(sites):3d} sites in {len(meth):3d} methods')
        for (cid, nm, e), n in sorted(meth.items(), key=lambda kv: (kv[0][0], kv[0][2])):
            at = ' '.join(hex(q) for q, c2, n2, e2 in sites if e2 == e) if all_sites else ''
            print(f'    class {cid:#05x} {nm:40s} entry {e:#08x} x{n} {at}')


stock = member(X3 / 'addon/04.cat', 'L/x3story.obj')
latest = None
for cat in sorted((X3 / 'addon').glob('*.cat')):
    b = member(cat, 'L/x3story.obj')
    if b: latest = (cat.relative_to(X3), b)
all_sites = '--all-sites' in sys.argv
scan('addon/04.cat L/x3story.obj (stock AP)', stock, all_sites)
if latest and latest[1] != stock:
    scan(f'{latest[0]} L/x3story.obj (installed Mayhem 4 override)', latest[1], all_sites)
