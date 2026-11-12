#!/usr/bin/env python3
"""Per-ship lateral hull profiles along the long axis (z) for the shield bubble's lathe (docs/architecture/
weapon-effects.md 5.1.1 "Ship profiles (2026-11-12)"); written by tools/effects/weapon_effects_table.py as
tables.ship_profiles (schema 3).

One entry per TShips data row (the edition's winning types/TShips: AP addon/, FL addon2/) whose scene loads:
  tships_index  0-based data row index (the first line, the table header, is not a row)
  scene         column 16 as written (`ships\\argon\\argon_M2_scene`; a number N > 0 names the cut scene cut\\%05d)
  class         column 52 as written (`OBJ_SHIP_M2`)
  units 'scene', axis 'z'
  z0, z1        long-axis extent of the whole hull
  slices        SLICES entries [cx, cy, hx, hy]: slice i spans [z0 + i (z1 - z0) / SLICES, z0 + (i + 1) (z1 - z0) /
                SLICES) (the last one closed), lateral box centre and half-widths of the points inside it; a slice
                with no point interpolates its nearest non-empty neighbours linearly in i (an end slice copies its one
                neighbour)
  radius        max distance of any point from the scene origin
  points        point count used
Floats are rounded to 3 decimals.

Units. One scene unit is one engine (world) unit: the unit of a scene part's key position and of a body's points
scaled by its LOD-0 value, point x value / 65536 (engine_bodies.py's body units; the engine stores LOD points
normalised to max |coordinate| 65536). The runtime's part half-extent ((ext x value + 0x8000) >> 16) x axis scale /
0x10000 (src/proxy/weapon_effects_bubble.h, hull_scale16 with a 0x10000 scale = 1.0) is the same number for a node of
unit scale; weapon_catalog.UNITS_PER_M = 500 per metre; the bubble's view units are engine units x core::world_scale.

Geometry. Every part of the scene with a body (B token; a number N -> v\\%05d; -1 or absent: none), LOD 0 of the body
(bob1.lods(tree)[0]), the points of the faces whose area is at least engine_bodies.VISIBLE_SHARE of the body's
largest face (engine_bodies.visible_bbox's rule: degenerate pin triangles dropped). Bodies under effects\\ (engine
glow and jet bodies, which the engine scales by (1, 1, 0.005), scene_radius.py) are excluded. A part is placed by
its first key: flags (hex or decimal), then x; y; z unless flags & 0x10 (a rotation-only key, position 0), then the
rotation a; ax; ay; az: a turns about the axis (ax, ay, az), the identity for a zero angle or axis (key layout and the
turn unit inferred from the shipped scenes, the loader is not traced; the rotation's sign convention is inferred, it
matters only for off-centre rotated parts). A part with `F p` is placed in the frame of the part numbered p (authored P
numbers; a missing parent or a cycle places it at the root). No scale channel is read.

A row is skipped (generated_from.ship_profiles_skipped: index, scene, class, reason) when: short_row (fewer than 53
fields), no_scene (scene token 0 or empty), not_found, refused:<code> (bob1.BodyRefused), cut1_scene (a binary
CUT1 scene, not decoded), format:<message>, no_points (no part yields a point).
Provenance (generated_from.ship_profiles): the TShips source, member and decoded sha256; rows, profiles, skipped;
scenes (distinct scene names tried), bodies (distinct part bodies loaded); scene_parts, effect_parts_excluded and
parts_unresolved (a body that does not resolve or parse: dummies such as v\\19xxx) tallied over the profiled scenes.

Reading. LOD 0 of a BOB1 body is read with numpy (lod0_binary: nothing past LOD 0's /PAR is parsed, so damage in a
later LOD goes unnoticed; mixed point flags fall back to bob1.parse), and catalogue members are decoded by read_member
(bytes.translate for the XOR, not cached) instead of Assets.read_entry's per-byte loop: the FL view takes about 10 s
for the section, most of it file reads and gzip.
"""
import gzip
import math
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/effects'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bob1  # noqa: E402
import engine_bodies  # noqa: E402
import weapon_effects_census as census_reader  # noqa: E402

SLICES = 32
TSHIPS_SCENE, TSHIPS_CLASS = 16, 52
EFFECT_PREFIX = 'effects'
PART = re.compile(r'^[ \t]*P[ \t]+(-?\d+)[ \t]*;([^\r\n]*)', re.M)
KEY = re.compile(r'\{([^}]*)\}')
NO_POSITION = 0x10


class SceneError(Exception):
    def __init__(self, reason):
        super().__init__(reason)
        self.reason = reason


def scene_name(token):
    """The resolver name of a TShips scene token: a number N > 0 -> cut\\%05d, 0 or empty -> None."""
    token = token.strip()
    if re.fullmatch(r'-?\d+', token):
        n = int(token)
        return None if n <= 0 else 'cut\\%05d' % n
    return token or None


def part_body_name(token):
    """A part's B token -> body name: N -> v\\%05d, -1 / absent -> None."""
    if token is None:
        return None
    token = token.strip()
    if re.fullmatch(r'-?\d+', token):
        n = int(token)
        return None if n < 0 else 'v\\%05d' % n
    return token or None


def parse_key(body):
    """(position (3,), rotation matrix (3, 3)) of one key's text between the braces; None when unreadable."""
    toks = [t.strip() for t in body.split(';') if t.strip()]
    try:
        flags = int(toks[0], 0)
        if flags & NO_POSITION:
            pos, rot = (0.0, 0.0, 0.0), [float(t) for t in toks[1:5]]
        else:
            pos, rot = tuple(float(t) for t in toks[1:4]), [float(t) for t in toks[4:8]]
    except (ValueError, IndexError):
        return None
    if len(rot) != 4:
        return None
    return np.array(pos, np.float64), rotation(*rot)


def rotation(turns, ax, ay, az):
    """The matrix of a turns about (ax, ay, az) (right-handed formula; identity for a zero angle or axis)."""
    n = math.sqrt(ax * ax + ay * ay + az * az)
    if n == 0 or turns == 0:
        return np.eye(3)
    x, y, z = ax / n, ay / n, az / n
    t = 2 * math.pi * turns
    c, s = math.cos(t), math.sin(t)
    C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def scene_parts(text):
    """[dict(number, body, parent, pos, rot)] of a text scene; `/` comment lines dropped, the first key per part."""
    text = '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('/'))
    found = list(PART.finditer(text))
    out = []
    for i, m in enumerate(found):
        head = m.group(2).split('//', 1)[0]
        body = re.search(r'(?:^|;)\s*B\s+([^;]+);', head)
        parent = re.search(r'(?:^|;)\s*F\s+(-?\d+)\s*;', head)
        stop = found[i + 1].start() if i + 1 < len(found) else len(text)
        key = KEY.search(text, m.end(), stop)
        placed = parse_key(key.group(1)) if key else None
        out.append(dict(number=int(m.group(1)), body=body.group(1).strip() if body else None,
                        parent=int(parent.group(1)) if parent else None,
                        pos=placed[0] if placed else np.zeros(3), rot=placed[1] if placed else np.eye(3),
                        key=placed is not None))
    return out


def world_transforms(parts):
    """[(pos, rot)] per part in scene coordinates: the part's key composed with its parent chain."""
    by_number = {p['number']: i for i, p in enumerate(parts)}
    done = {}

    def place(i, seen):
        if i in done:
            return done[i]
        p = parts[i]
        j = by_number.get(p['parent']) if p['parent'] is not None else None
        if j is None or j in seen or j == i:
            done[i] = (p['pos'], p['rot'])
        else:
            pp, pr = place(j, seen | {i})
            done[i] = (pp + pr @ p['pos'], pr @ p['rot'])
        return done[i]
    return [place(i, frozenset()) for i in range(len(parts))]


def body_points(record):
    """(n, 3) LOD-0 points in body units (point x value / 65536) of the faces with area >= VISIBLE_SHARE of the
    largest (engine_bodies.visible_bbox's rule); an empty array without faces."""
    p = np.array([q[1:4] for q in record['points']], np.float64)
    faces = np.array([f[:3] for part in record['parts'] for g in part['groups'] for f in g['faces']], np.int64)
    return visible_points(record['value'], p, faces)


def visible_points(value, xyz, faces):
    """body_points on arrays: xyz (n, 3) stored point coordinates, faces (m, 3) point indices."""
    p = np.asarray(xyz, np.float64).reshape(-1, 3) * (value / 65536.0)
    faces = np.asarray(faces, np.int64).reshape(-1, 3)
    if not len(faces) or not len(p):
        return np.zeros((0, 3))
    a, b, c = p[faces[:, 0]], p[faces[:, 1]], p[faces[:, 2]]
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1) / 2
    keep = faces[area >= engine_bodies.VISIBLE_SHARE * area.max()]
    return p[np.unique(keep)]


class SlowPath(Exception):
    """lod0_binary cannot take its array path for this body; the caller parses it with bob1.parse."""


def lod0_binary(data):
    """(value, xyz (n, 3), faces (m, 3)) of a BOB1 body's LOD 0 read with numpy: bob1.parse_binary's layout up to the
    first LOD's /PAR (materials through bob1.read_material, nothing after LOD 0 read). Raises SlowPath when a point
    lacks a position or the points' flag words differ (bob1.parse handles those), bob1.FormatError on bad data."""
    r = bob1.Reader(data)
    r.expect('BOB1')
    while True:
        t = r.tag()
        if t in ('INFO', 'NAME'):
            r.cstr()
        elif t in ('VERS', 'SND1'):
            r.u32()
        elif t in bob1.MATVER:
            ver = bob1.MATVER[t]
            for _ in range(r.count()):
                bob1.read_material(r, ver)
        elif t == 'BODY':
            break
        else:
            raise SlowPath(t)
        r.expect('/' + t[:3])
    if r.u16() < 1:
        raise SlowPath('no LOD')
    value, _ = r.i32(), r.u32()
    if r.peek() == 'BONE':
        raise SlowPath('BONE')
    r.expect('POIN')
    n = r.count()
    d = r.d
    if n:
        r.need(2)
        flags = (d[r.o] << 8) | d[r.o + 1]
        if not flags & 1:
            raise SlowPath('point without a position')
        stride = 2 + bob1.point_struct(flags).size
        r.need(n * stride)
        block = np.frombuffer(d, np.uint8, n * stride, r.o).reshape(n, stride)
        if not (block[:, 0] == flags >> 8).all() or not (block[:, 1] == flags & 0xff).all():
            raise SlowPath('mixed point flags')
        xyz = block[:, 2:14].copy().view('>i4').reshape(n, 3)
        r.o += n * stride
    else:
        xyz = np.zeros((0, 3), np.int64)
    r.expect('/POI')
    if r.peek() == 'WEIG':
        raise SlowPath('WEIG')
    r.expect('PART')
    faces = []
    for _ in range(r.count()):
        pre = r.u32() & bob1.PART_PRECOMPUTED
        for _ in range(r.u16()):
            r.i32()
            m = r.count()
            r.need(16 * m)
            faces.append(np.frombuffer(d, '>i4', 4 * m, r.o).reshape(m, 4)[:, :3])
            r.o += 16 * m
            if pre:
                k = r.count()
                r.need(28 * k)
                r.o += 28 * k
        if pre:
            r.ints(10)
    r.expect('/PAR')
    return value, xyz, (np.concatenate(faces) if faces else np.zeros((0, 3), np.int64))


_XOR = {}


def _xor(data, k):
    table = _XOR.get(k)
    if table is None:
        table = _XOR[k] = bytes(i ^ k for i in range(256))
    return data.translate(table)


def read_member(assets, entry):
    """assets.read_entry's decoded bytes (catalogue XOR 0x33, then sector_fog_census.unpack's gzip forms) without
    its per-byte Python XOR and without caching them (the hull bodies run to hundreds of megabytes); an entry the
    assets already cache or a loose file goes through assets.read_entry."""
    cached = assets.cache.get((entry['source'], entry['path']))
    if cached is not None or 'loose' in entry:
        return cached if cached is not None else assets.read_entry(entry)
    with entry['cat'].with_suffix('.dat').open('rb') as stream:
        stream.seek(entry['offset'])
        raw = stream.read(entry['size'])
    if len(raw) != entry['size']:
        raise ValueError('Truncated archive member: ' + str((entry['source'], entry['path'])))
    data = _xor(raw, 0x33)
    if data.startswith(b'\x1f\x8b'):
        return gzip.decompress(data)
    if len(data) > 3:
        k = data[0] ^ 0xc8
        if _xor(data[1:3], k) == b'\x1f\x8b':
            return gzip.decompress(_xor(data[1:], k))
    return data


def load_body(assets, name, cache):
    """body_points of a body name (cached; None when it does not resolve or parse as a body)."""
    key = name.lower()
    if key not in cache:
        pts = None
        try:
            data = read_member(assets, bob1.resolve_body(assets, name))
            if data.startswith(b'BOB1'):
                try:
                    pts = visible_points(*lod0_binary(data))
                except SlowPath:
                    pts = None
            if pts is None and not data.startswith(b'CUT1'):
                lods = bob1.lods(bob1.parse(data, None))
                pts = body_points(lods[0]) if lods else None
        except (FileNotFoundError, bob1.FormatError, ValueError, KeyError, IndexError):
            pts = None
        cache[key] = pts
    return cache[key]


def scene_points(assets, name, cache):
    """All placed points of a scene, (n, 3); raises SceneError with the skip reason."""
    try:
        entry = bob1.resolve_body(assets, name)
    except FileNotFoundError:
        raise SceneError('not_found') from None
    except bob1.BodyRefused as exc:
        raise SceneError(f'refused:{exc.reason}') from None
    data = read_member(assets, entry)
    if data.startswith(b'CUT1'):
        raise SceneError('cut1_scene')
    parts = scene_parts(data.decode('latin1')) if not data.startswith(b'BOB1') else []
    if not parts:  # not a scene: a body named directly is a one-part scene at the origin
        pts = load_body(assets, name, cache)
        if pts is None:
            raise SceneError('format:neither a text scene nor a body')
        if not len(pts):
            raise SceneError('no_points')
        return pts, dict(parts=1, effects=0, unresolved=0)
    chunks, stats = [], dict(parts=len(parts), effects=0, unresolved=0)
    for p, (pos, rot) in zip(parts, world_transforms(parts)):
        body = part_body_name(p['body'])
        if body is None:
            continue
        if body.replace('/', '\\').lower().split('\\', 1)[0] == EFFECT_PREFIX:
            stats['effects'] += 1
            continue
        pts = load_body(assets, body, cache)
        if pts is None:
            stats['unresolved'] += 1
            continue
        if len(pts):
            chunks.append(pts @ rot.T + pos)
    if not chunks:
        raise SceneError('no_points')
    return np.concatenate(chunks), stats


def r3(x):
    return round(float(x), 3)


def profile(points, slices=SLICES):
    """dict(z0, z1, slices, radius, points) of an (n, 3) point set (see the module doc)."""
    z = points[:, 2]
    z0, z1 = float(z.min()), float(z.max())
    span = z1 - z0
    idx = np.zeros(len(z), np.int64) if span <= 0 else np.minimum(((z - z0) / span * slices).astype(np.int64),
                                                                     slices - 1)
    rows = [None] * slices
    for i in range(slices):
        q = points[idx == i]
        if len(q):
            lo, hi = q[:, :2].min(0), q[:, :2].max(0)
            rows[i] = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (hi[0] - lo[0]) / 2, (hi[1] - lo[1]) / 2]
    filled = [i for i in range(slices) if rows[i] is not None]
    for i in range(slices):
        if rows[i] is None:
            a = max((j for j in filled if j < i), default=None)
            b = min((j for j in filled if j > i), default=None)
            if a is None or b is None:
                rows[i] = list(rows[a if b is None else b])
            else:
                t = (i - a) / (b - a)
                rows[i] = [rows[a][k] + t * (rows[b][k] - rows[a][k]) for k in range(4)]
    return dict(z0=r3(z0), z1=r3(z1), slices=[[r3(v) for v in row] for row in rows],
                radius=r3(np.sqrt((points ** 2).sum(1)).max()), points=int(len(points)))


def ship_profiles(assets):
    """(entries, skipped, provenance) over the winning types/TShips of the assets' view."""
    data, prov = assets.get('types/TShips.txt')
    rows = census_reader.data_rows(data.decode('latin1'))[1:]
    entries, skipped, cache, scenes = [], [], {}, {}
    totals = dict(parts=0, effects=0, unresolved=0)
    for i, f in enumerate(rows):
        scene = f[TSHIPS_SCENE] if len(f) > TSHIPS_SCENE else ''
        cls = f[TSHIPS_CLASS] if len(f) > TSHIPS_CLASS else ''
        base = dict(tships_index=i, scene=scene, **{'class': cls})
        if len(f) <= TSHIPS_CLASS:
            skipped.append(dict(base, reason='short_row'))
            continue
        name = scene_name(scene)
        if name is None:
            skipped.append(dict(base, reason='no_scene'))
            continue
        key = name.lower()
        if key not in scenes:
            try:
                pts, stats = scene_points(assets, name, cache)
                scenes[key] = profile(pts)
                for k in totals:
                    totals[k] += stats[k]
            except SceneError as exc:
                scenes[key] = exc.reason
            except (bob1.FormatError, ValueError, UnicodeDecodeError) as exc:
                scenes[key] = f'format:{str(exc)[:160]}'
        got = scenes[key]
        if isinstance(got, str):
            skipped.append(dict(base, reason=got))
        else:
            entries.append(dict(base, units='scene', axis='z', **got))
    provenance = dict(tships=dict(source=prov['source'], member=prov['member'], sha256=prov['decoded_sha256']),
                      rows=len(rows), profiles=len(entries), skipped=len(skipped), scenes=len(scenes),
                      bodies=sum(1 for v in cache.values() if v is not None), scene_parts=totals['parts'],
                      effect_parts_excluded=totals['effects'], parts_unresolved=totals['unresolved'], slices=SLICES)
    return entries, skipped, provenance
