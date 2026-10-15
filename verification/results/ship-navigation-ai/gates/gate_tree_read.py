#!/usr/bin/env python3
"""The DLL's gate collision-tree read (site E, ship_avoidance_walk_core.h gate_tree_state) against the engine: the node chain
under [gate+0x70], where the tree lives, what a gate without one holds, and the depth/width of every TGates scene's part tree.

Owning note: docs/reverse-engineering/ship-navigation-ai.md section 10.9 ("Collision-tree read"). Read-only on the game folder;
prints addresses, derived names, counts and depths only (no game bytes).

    python3 -I verification/results/ship-navigation-ai/gates/gate_tree_read.py \
        > verification/results/ship-navigation-ai/gates/gate_tree_read.txt
"""
import hashlib, re, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/game-scale'))
from sector_fog_census import Assets, STOCK_AP_CATALOGUES   # noqa: E402
import ships, scene_radius as sr                             # noqa: E402

GAME = Path('/mnt/c/X3')
EXE = GAME / 'X3AP.exe'
d = EXE.read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def rd(va, n):
    r = va - base
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs):
            o = ra + r - sva
            return d[o:o + n]
    raise ValueError(hex(va))


BYTES = [
    # object creation 0x0043ffa0, class 0x12
    (0x004410f0, '66 3b c3', 'create: class [esp+0x160] vs 0 (3, 0x15, 0, 1 have own branches; 0x12 falls to 0x00441255)'),
    (0x00441255, 'e8 b6 5a 04 00', 'create: generic branch, root = 0x00486d10() (0x270-byte node)'),
    (0x00441261, '89 45 70', '... [obj+0x70] = root'),
    (0x00441280, 'e8 0b d9 03 00', '... 0x0047eb90(root, 0): the root body tree (TGates column 0)'),
    (0x00441686, '0f bf 55 4a', 'create: switch case 0x12 (jump table 0x00441db4/0x00441dec): type [obj+0x4a]'),
    (0x0044168a, 'a1 00 70 60 00', '... class-0x12 type table [0x00607000] (= 0x00606fb8 + 4 * 0x12)'),
    (0x00441695, '8b 4c 02 44', '... scene id = TGates record +0x44'),
    (0x0044169b, '89 8d 98 00 00 00', '... [obj+0x98] = scene id'),
    (0x004416a1, 'e8 ea ef 00 00', '... call 0x00450690 (scene parts under the root, synchronous)'),
    (0x004506a6, 'e8 85 c7 fe ff', '0x00450690: 0x0043ce30(ctx, [obj+0x98], [obj+0x70], obj, 0)'),
    (0x004506af, '89 86 98 00 00 00', '... returns 0 (scene not found) -> [obj+0x98] = 0'),
    # scene part nodes
    (0x0043cf9e, '83 f8 ff', '0x0043ce30: part body -1 -> no node'),
    (0x0043cfb1, '3b c1', '... part body == non-zero root body -> the part is the root itself (no node)'),
    (0x0043d3b6, 'e8 55 99 04 00', '0x0043d1d0: part node = 0x00486d10()'),
    (0x0043d3dc, 'e8 ef 9b 04 00', '... parent = 0x00486fd0(root, scene, F) when part F >= 0, else the root'),
    (0x0043d3e9, 'e8 32 cb 04 00', '... 0x00489f20: link under the parent'),
    (0x0043d3f5, 'e8 46 17 05 00', '... 0x0048eb40: [c+0x258] = [scene+0] (scene id), [c+0x25c] = part index'),
    (0x0048eb43, '89 86 5c 02 00 00', '0x0048eb40: [c+0x25c] = part index'),
    (0x0048eb4c, '89 8e 58 02 00 00', '0x0048eb40: [c+0x258] = scene id'),
    (0x004927f0, '89 06', 'scene load 0x00491b10: [scene+0] = its id (arg 1)'),
    (0x00486ff0, '8b 8e 58 02 00 00 3b 0f', '0x00486fd0 (find part node): [c+0x258] == [scene+0]'),
    (0x0043d7e7, '83 7d 18 00', '0x0043d1d0: arg 5 != 0 -> no body, no tree (0x00450690 passes 0)'),
    (0x0043d889, '8b 83 2c 01 00 00', '... flags & 0x10004000, jet, dock/0x4a56 -> 0x8000000, no tree'),
    (0x0043d8e6, 'e8 a5 12 04 00', '... else 0x0047eb90(node, coarsest LOD | 1)'),
    # node list layout
    (0x00486d8b, '8d 43 0c', '0x00486d10 init: list head at node+0xc ...'),
    (0x00486d8e, '89 40 08', '... [node+0x14] (tail) = node+0xc'),
    (0x00486d97, '89 08', '... [node+0xc] = node+0x10; [node+0x10] = 0 (sentinel)'),
    (0x00489f4c, '8d 47 10 89 06', '0x00489f20 link: [c] = parent+0x10 (the new last child points at the sentinel)'),
    (0x00489f54, '89 7e 18', '... [c+0x18] = parent'),
    (0x0048aa46, '8b 7b 0c 83 3f 00', 'narrow phase 0x0048a890: c = [n+0xc]; while [c] != 0 (same walk as the station walk)'),
    # where the tree is, and the flag
    (0x0047ebae, 'e8 0d 78 00 00', '0x0047eb90: model = 0x004863c0([node+0x140])'),
    (0x0047ec27, '39 6e 5c', '... [model+0x5c] != 0 (tree exists for this body) -> only set the node flag'),
    (0x0047ed0d, 'e8 5e 3c 06 00', '... 0x004e2970: new RAPID model (0x18 bytes) ...'),
    (0x0047ed16, '89 46 5c', '... [model+0x5c] = it'),
    (0x0047eec7, 'e8 b4 1d 06 00', '... 0x004e0c80: build ([model+0x5c]) after every triangle is added'),
    (0x0047eed0, '81 8f 2c 01 00 00 00 00 00 01', '... then [node+0x12c] |= 0x1000000'),
    (0x0047eeda, '89 8f b4 01 00 00', '... and [node+0x1b4] = requested LOD'),
    (0x0047ece8, 'e8 13 00 ff ff', '... no colliding group: 0x0046ed00 marks the body slot -1 ...'),
    (0x0047ecf5, '81 8f 2c 01 00 00 00 00 00 08', '... and only if that failed (id out of slot range) node |= 0x8000000'),
    (0x0048a92f, 'f7 c1 00 00 00 01', 'narrow phase: both nodes 0x1000000 ...'),
    (0x0048a96e, 'e8 4d ba ff ff', '... models via 0x004863c0 ...'),
    (0x0047f1b4, '8b 52 5c', '... 0x0047f1b0 reads [model+0x5c] of both, returns 0 if either is null'),
]
fails = 0
print('Gate collision-tree read (owning note: docs/reverse-engineering/ship-navigation-ai.md section 10.9)')
print('EXE %s sha256 %s' % (EXE, hashlib.sha256(d).hexdigest()[:16]))
for va, hexs, claim in BYTES:
    ok = rd(va, len(bytes.fromhex(hexs))) == bytes.fromhex(hexs)
    fails += not ok
    print('  %s %#010x %-32s %s' % ('ok  ' if ok else 'FAIL', va, hexs, claim))

# writers of the two flags: every immediate OR/AND on +0x12c that touches 0x1000000 or 0x8000000 (register-operand writers not
# classified: they OR/AND values computed elsewhere)
vs, sva, rs, ra = secs[0]
text = d[ra:ra + rs]
print('\n-- immediate writers of [node+0x12c] bits 0x1000000 / 0x8000000 (.text scan)')
for m in re.finditer(rb'\x81[\x80-\x87\x88-\x8f\xa0-\xa7]\x2c\x01\x00\x00', text):
    i = m.start()
    reg = (text[i + 1] >> 3) & 7
    imm = struct.unpack_from('<I', text, i + 6)[0]
    op = {1: 'or', 4: 'and'}.get(reg)
    if op == 'or' and imm & 0x09000000:
        print('  %#010x or  %#010x' % (base + sva + i, imm))
    if op == 'and' and (~imm) & 0x09000000:
        print('  %#010x and %#010x (clears %#x)' % (base + sva + i, imm, (~imm) & 0x09000000))

# ---------------------------------------------------------------- the part tree of every TGates scene
DEPTH_DLL = 3  # gate_tree_walk examines children at depths 1..3 below the root


def body_id(name):
    return int(name) if name and name.isdigit() else None


print('\n-- TGates scenes: part nodes under the root (depth 1 = child of the root); the DLL walk sees depths 1..%d' % DEPTH_DLL)
worst = 0
for label, a in (('stock', Assets(GAME, catalogues=STOCK_AP_CATALOGUES)), ('installed', Assets(GAME))):
    _, rows, prov = ships.table(a, 'TGates')
    scenes = {}
    for i, row in enumerate(rows):
        scenes.setdefault((row[7], row[0]), []).append(i)
    for (scene, col0), idx in scenes.items():
        txt, src = sr.scene_text(a, scene)
        if txt is None:
            print('  %-9s rows %-14s %-40s scene text not found (binary or missing)' % (label, ','.join(map(str, idx)), scene))
            worst = max(worst, 99)
            continue
        ps = sr.parts(txt)
        byidx = {p['idx']: p for p in ps}
        root_id = int(col0)
        nodes = []
        for p in ps:
            if p['body'] is None or p['body'].strip() == '-1':
                continue
            if root_id and body_id(p['body']) == root_id:
                continue  # the root itself (0x0043cfb1)
            depth, q, seen = 1, p, set()
            while q['parent'] >= 0 and q['parent'] in byidx and q['parent'] not in seen:
                seen.add(q['parent']); q = byidx[q['parent']]; depth += 1
            nodes.append((p['idx'], depth, p['body']))
        md = max((n[1] for n in nodes), default=0)
        worst = max(worst, md)
        kids = {}
        for p in ps:
            kids[p['parent']] = kids.get(p['parent'], 0) + 1
        print('  %-9s rows %-14s %-40s %s  nodes %d  max depth %d  max siblings %d  %s'
              % (label, ','.join(map(str, idx)), scene, src, len(nodes), md, max(kids.values(), default=0),
                 '; '.join('p%d d%d %s' % n for n in nodes)))
print('\nmax part depth over every TGates scene, both views: %d (DLL walk depth %d: %s)'
      % (worst, DEPTH_DLL, 'covered' if worst <= DEPTH_DLL else 'NOT covered'))
print('RESULT %s (%d byte strings)' % ('PASS' if not fails else 'FAIL %d' % fails, len(BYTES)))
