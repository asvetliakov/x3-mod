"""FL run 67 triage: the Argon Titan's scene parts offline (FL addon2 view), with each part's body value, parent,
first key frame (position and the remaining key fields: rotation), and the scene_radius estimate.
Usage: python3 -I verification/results/fl-run67/titan_scene.py [name-substring]   (prints rows only)"""
import re, sys
from pathlib import Path
R = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(R / 'tools/analysis'), str(R / 'verification/results/game-scale')]
from sector_fog_census import Assets, names
import scene_radius as sr

A = Assets(Path('/mnt/c/X3FL'), edition='fl')
want = (sys.argv[1] if len(sys.argv) > 1 else 'titan').lower()
for k in ('addon2/types/TShips.txt', 'addon2/types/TShips.pck', 'types/TShips.txt', 'types/TShips.pck'):
    if A.candidates(k):
        d, prov = A.get(k); break
rows = [l.split(';') for l in d.decode('latin-1').splitlines() if l.strip() and not l.lstrip().startswith('/')][1:]
res, _ = names(A)
for i, r in enumerate(rows):
    if len(r) < 53: continue
    nm = res(17, int(r[6])) if r[6].lstrip('-').isdigit() else ''
    if want in (nm or '').lower() or want in r[16].lower():
        print('ROW', i, repr(nm), 'scene', r[16], 'id', r[-2] if r[-1] == '' else r[-1], 'radii', sr.radii(A, r[16]))

# The parts of the matched scene(s): index, parent, body, body value, first key frame fields.
KEYS = re.compile(r'\{\s*(0x[0-9a-fA-F]+)\s*;([^}]*)\}')
for i, r in enumerate(rows):
    if len(r) < 53: continue
    nm = res(17, int(r[6])) if r[6].lstrip('-').isdigit() else ''
    if not (want in (nm or '').lower() or want in r[16].lower()): continue
    text, src = sr.scene_text(A, r[16])
    if text is None: print('NOSCENE', r[16], src); continue
    ms = list(sr.PART.finditer(text))
    for j, m in enumerate(ms):
        end = ms[j + 1].start() if j + 1 < len(ms) else len(text)
        head = m.group(2).strip()
        k = KEYS.search(text, m.end(), end)
        body = re.search(r'\bB\s+([^;]+);', head)
        bv = sr.body_value(A, body.group(1).strip()) if body else 0
        f = [x.strip() for x in k.group(2).split(';') if x.strip()] if k else []
        print('PART', m.group(1), '|', head[:80], '| value', bv, '| key', k.group(1) if k else '-', ' '.join(f[:12]))
    break

# Per body: per-LOD value and max |coordinate| per axis (the LOD extent's stand-in: the engine stores max |centre| + half
# over the mesh parts, about the model origin), the half-extent e_i = ((ext_i * s + 0x8000) >> 16) with s = LOD-0 value.
import bob1
def body_ext(name):
    e = bob1.resolve_body(A, name)
    tree = bob1.parse(A.read_entry(e))
    out = []
    for lod in bob1.lods(tree):
        m = [0, 0, 0]
        for p in lod['points']:
            if p[0] & 1:
                for i in range(3): m[i] = max(m[i], abs(p[1 + i]))
        out.append((lod['value'], m))
    return out
for name in ('ships\\argon\\argon_M2', 'ships\\props\\bigturret_dummy', 'ships\\props\\bigturret2_dummy'):
    try:
        L = body_ext(name)
    except Exception as x:
        print('BODY', name, 'error', type(x).__name__, x); continue
    s = L[0][0]
    ext = [max(l[1][i] for l in L) for i in range(3)]
    print('BODY', name, 'lods', len(L), 'values', [l[0] for l in L], 'ext_max', ext,
          'half_extent', [(x * s + 0x8000) >> 16 for x in ext])
# Per LOD of the hull, and the hull's half-width profile along z (max |x|, |y| of LOD-0 points in 10 z slabs, engine
# units after the scale): how far an ellipsoid on the box's x/y half-extents sits off the hull at each station.
L0 = None
e = bob1.resolve_body(A, 'ships\\argon\\argon_M2')
tree = bob1.parse(A.read_entry(e))
for k, lod in enumerate(bob1.lods(tree)):
    m = [0, 0, 0]; lo = [1 << 40] * 3; hi = [-(1 << 40)] * 3
    for p in lod['points']:
        if p[0] & 1:
            for i in range(3):
                m[i] = max(m[i], abs(p[1 + i])); lo[i] = min(lo[i], p[1 + i]); hi[i] = max(hi[i], p[1 + i])
    print('HULL_LOD', k, 'value', lod['value'], 'points', len(lod['points']), 'maxabs', m, 'lo', lo, 'hi', hi)
    if k == 0: L0 = lod
s = L0['value']
pts = [(p[1] * s / 65536, p[2] * s / 65536, p[3] * s / 65536 + 17329) for p in L0['points'] if p[0] & 1]
zs = [p[2] for p in pts]; z0, z1 = min(zs), max(zs)
for j in range(10):
    a, b = z0 + (z1 - z0) * j / 10, z0 + (z1 - z0) * (j + 1) / 10
    sl = [p for p in pts if a <= p[2] <= b]
    if sl:
        print('HULL_SLAB z=%d..%d n=%d max|x|=%d y=%d..%d' % (a, b, len(sl), max(abs(p[0]) for p in sl),
              min(p[1] for p in sl), max(p[1] for p in sl)))
