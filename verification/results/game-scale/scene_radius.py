"""Static estimate of the engine collision radius [obj+0xa4] of a type-table object from its scene.

Engine rule (docs/reverse-engineering/game-scale-and-ship-classes.md section 2): 0x00487e30 sets node
+0x70 = +0xa0 = LOD-0 value of the body (its scale: LOD-0 points are normalised to max |coord| = 65536
and scaled by this value); 0x00488270 multiplies +0xa0 by the largest node scale; 0x00488170 computes
radius(node) = max(+0xa0, max over children (radius(child) + max_axis |child local position|)) and caches it
in +0xa4. The object root of a ship is TShips column 0 (body 0 'testbody', value 47 on 524/525 rows).

Here: parts of a text scene 'P n; B body; [C c;] [F parent;] ...' with the first key frame
'{ 0x2002; x; y; z; ...}' as the local position; children of a part with 'F p' nest under p.
Numeric body names resolve to objects/v/%05d (0x0046e400: a digit string is its own id); a body that does
not resolve (dummy ids, missing members) contributes value 0. Jet scale (1,1,0.005) keeps max scale 1.
Not modelled: part rotation, dummy cut scenes (dock ports) and animated key frames.
"""
import re, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
import bob1

PART = re.compile(r'^\s*P\s+(\d+)\s*;(.*?)$', re.M)
KEY = re.compile(r'\{\s*0x[0-9a-fA-F]+\s*;\s*(-?[\d.]+)\s*;\s*(-?[\d.]+)\s*;\s*(-?[\d.]+)\s*;')


def scene_text(assets, scene_name):
    stem = 'objects/' + scene_name.replace('\\', '/').strip('/')
    for ext in ('.pbb', '.bob', '.pbd', '.bod'):
        c = assets.candidates(stem + ext)
        if c:
            data = assets.read_entry(c[-1])
            if data[:4] == b'CUT1':
                return None, c[-1]['source']
            return data.decode('latin-1'), c[-1]['source']
    return None, None


def parts(text):
    out = []
    ms = list(PART.finditer(text))
    for i, m in enumerate(ms):
        idx = int(m.group(1)); head = m.group(2)
        body = re.search(r'\bB\s+([^;]+);', head)
        par = re.search(r'\bF\s+(-?\d+)\s*;', head)
        cflag = re.search(r'\bC\s+(\d+)\s*;', head)
        end = ms[i + 1].start() if i + 1 < len(ms) else len(text)
        k = KEY.search(text, m.end(), end)
        pos = tuple(int(float(v)) for v in k.groups()) if k else (0, 0, 0)
        out.append(dict(idx=idx, body=body.group(1).strip() if body else None,
                        parent=int(par.group(1)) if par else -1, c=int(cflag.group(1)) if cflag else 0, pos=pos))
    return out


_BV = {}


def body_value(assets, name):
    if not name:
        return 0
    key = name.lower()
    if key in _BV:
        return _BV[key]
    v = 0
    ref = ('v/%05d' % int(name)) if name.isdigit() else name
    try:
        e = bob1.resolve_body(assets, ref)
        data = assets.read_entry(e)
        if data[:4] == b'BOB1':
            r = bob1.Reader(data); r.expect('BOB1')
            while True:
                t = r.tag()
                if t in ('INFO', 'NAME'): r.cstr()
                elif t in ('VERS', 'SND1'): r.u32()
                elif t in bob1.MATVER:
                    ver = bob1.MATVER[t]
                    for _ in range(r.count()): bob1.read_material(r, ver)
                elif t == 'BODY':
                    r.u16(); v = r.i32(); break
                else:
                    break
                r.expect('/' + t[:3])
        else:
            tr = bob1.TextReader(data)
            while not tr.done() and tr.peek().upper().startswith('MATERIAL'):
                k, _, first = tr.next().partition(':')
                bob1._read_text_material(tr, k.strip().upper(), first.strip())
            v = tr.int('body size')
    except Exception:
        v = 0
    _BV[key] = v
    return v


EFFECT_PREFIX = ('effects\\', 'effects/')
DUMMY_HINT = ('dummy',)


def radii(assets, scene_name):
    """(engine_radius_estimate, hull_value, n_parts, source) in engine units; None if unavailable."""
    text, src = scene_text(assets, scene_name)
    if text is None:
        return None
    ps = parts(text)
    if not ps:
        return None
    kids = {}
    for p in ps:
        kids.setdefault(p['parent'], []).append(p)

    def rad(p, depth=0):
        r = body_value(assets, p['body'])
        if depth < 32:
            for c in kids.get(p['idx'], []):
                if c is p:
                    continue
                r = max(r, rad(c, depth + 1) + max(abs(v) for v in c['pos']))
        return r
    root = 47   # TShips column 0 body 0 'testbody' (measured value 47)
    for p in kids.get(-1, []):
        root = max(root, rad(p) + max(abs(v) for v in p['pos']))
    # hull: the largest part body that is not an effect or a dummy, plus its offset
    hull = 0
    for p in ps:
        b = (p['body'] or '').lower()
        if b.startswith(EFFECT_PREFIX) or any(h in b for h in DUMMY_HINT) or b.isdigit():
            continue
        hull = max(hull, body_value(assets, p['body']) + max(abs(v) for v in p['pos']))
    return root, hull, len(ps), src
