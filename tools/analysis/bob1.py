#!/usr/bin/env python3
"""BOB1 binary body (.pbb/.bob) reader and writer.

Layout from the engine parser X3AP.exe 0x00481aa0, tag table 0x0054ed50
(docs/reverse-engineering/body-format-bob1.md). All values are big-endian.
parse() returns a plain tree; serialise() writes it back byte for byte. Every
opaque field (material words, point u32, face word, per-group 7-int records,
the 10 part ints) is kept as read.

Tree:
  {'sections': [(tag, value), ...]}   tags in file order:
    INFO/NAME -> bytes, VERS/SND1 -> int, MAT3/MAT5/MAT6 -> [material dict],
    BODY -> [lod]
  lod  = {'value', 'flags', ['bones'], 'points', ['weights'], 'parts'}
         value: LOD 0 = object scale (LODrec+0x14), LOD >= 1 = threshold (LODrec+0x34)
  point = tuple (point flags, *ints) in file order (see point_struct)
  part = {'flags', 'groups', ['bounds']}; group = {'material', 'faces', ['extra']}
         faces: list of 4-int tuples (a, b, c, face word)
         extra/bounds only when part flags & 0x10000000

CLI (prints derived numbers only; never writes body bytes):
  python3 tools/analysis/bob1.py info <file | archive member or stem>
  python3 tools/analysis/bob1.py audit [--summary] [--json OUT] [--view-distance V] [--factor F]
"""
import argparse
import os
import struct
import sys
from pathlib import Path

PART_PRECOMPUTED = 0x10000000        # part flag: per-group 7-int records + 10 part ints
EFFECT_MATERIAL = 0x02000000         # MAT6 flag: effect material (0x00470490)
MATVER = {'MAT3': 3, 'MAT5': 5, 'MAT6': 6}
# SPTYPE_* value sizes in 32-bit words (0x00470490; table 0x0054f0a0); 8 = string
SPTYPE_WORDS = {0: 1, 1: 1, 2: 1, 3: 2, 4: 3, 5: 4, 6: 9, 7: 16}
DEFAULT_GAME = Path(os.path.expanduser(
    '~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'))
BODY_EXTENSIONS = ('.pbb', '.bob', '.pbd', '.bod')     # 0x004863c0 search order


class FormatError(ValueError):
    pass


def point_struct(flags, _cache={}):
    """Struct for one POIN record body after its u16 flag word (0x00482640)."""
    s = _cache.get(flags)
    if s is None:
        fmt = '>'
        if flags & 1: fmt += '3i'                      # position
        if flags & 2:
            fmt += '2i'                                # uv 16.16
            if flags & 4: fmt += '2i'                  # second uv 16.16
        if flags & 8: fmt += '3i'                      # normal 16.16
        if flags & 0x10: fmt += 'I'                    # u32 -> point+0x14
        s = _cache[flags] = struct.Struct(fmt)
    return s


class Reader:
    def __init__(self, data):
        self.d, self.o = data, 0

    def need(self, n):
        if self.o + n > len(self.d):
            raise FormatError(f'truncated at {self.o:#x} (need {n} bytes)')

    def tag(self):
        self.need(4)
        t = self.d[self.o:self.o + 4].decode('latin1'); self.o += 4
        return t

    def expect(self, t):
        at = self.o
        got = self.tag()
        if got != t:
            raise FormatError(f'expected {t!r}, got {got!r} at {at:#x}')

    def peek(self):
        return self.d[self.o:self.o + 4].decode('latin1')

    def unpack(self, fmt):
        s = struct.Struct(fmt) if isinstance(fmt, str) else fmt
        self.need(s.size)
        v = s.unpack_from(self.d, self.o); self.o += s.size
        return v

    def u16(self): return self.unpack('>H')[0]
    def i32(self): return self.unpack('>i')[0]
    def u32(self): return self.unpack('>I')[0]

    def count(self):
        n = self.i32()
        if n < 0:
            raise FormatError(f'negative count {n} at {self.o - 4:#x}')
        return n

    def ints(self, n): return list(self.unpack('>%di' % n))

    def cstr(self):
        e = self.d.find(b'\0', self.o)
        if e < 0:
            raise FormatError(f'unterminated string at {self.o:#x}')
        s = self.d[self.o:e]; self.o = e + 1
        return s


class Writer:
    def __init__(self):
        self.b = bytearray()

    def tag(self, t): self.b += t.encode('latin1')
    def u16(self, v): self.b += struct.pack('>H', v)
    def i32(self, v): self.b += struct.pack('>i', v)
    def u32(self, v): self.b += struct.pack('>I', v)
    def ints(self, v): self.b += struct.pack('>%di' % len(v), *v)
    def cstr(self, s): self.b += s + b'\0'


def read_material(r, ver):
    m = {'index': r.u16()}
    if ver >= 6:
        m['flags'] = r.u32()
        if m['flags'] & EFFECT_MATERIAL:
            m['technique'] = r.u16(); m['effect'] = r.cstr(); params = []
            for _ in range(r.u16()):
                name = r.cstr(); typ = r.u16()
                if typ != 8 and typ not in SPTYPE_WORDS:
                    raise FormatError(f'unknown effect parameter type {typ} at {r.o - 2:#x}')
                params.append((name, typ, r.cstr() if typ == 8 else r.ints(SPTYPE_WORDS[typ])))
            m['params'] = params
            return m
        m['texture'] = r.cstr()
    else:
        m['texture'] = r.u16()
    m['colors'] = [r.u16() for _ in range(12)]      # +0x04..+0x1a
    m['w24'] = r.u16(); m['w26'] = r.u16()
    if ver < 6:
        m['flagword'] = r.u16()
    m['w2c'] = r.u16()
    m['maps'] = [((r.cstr() if ver >= 6 else r.u16()), r.u16()) for _ in range(3)]
    if ver >= 6:
        m['extra'] = [(r.cstr(), r.u16()) for _ in range(2)]
    return m


def write_material(w, m, ver):
    w.u16(m['index'])
    if ver >= 6:
        w.u32(m['flags'])
        if m['flags'] & EFFECT_MATERIAL:
            w.u16(m['technique']); w.cstr(m['effect']); w.u16(len(m['params']))
            for name, typ, val in m['params']:
                w.cstr(name); w.u16(typ)
                w.cstr(val) if typ == 8 else w.ints(val)
            return
        w.cstr(m['texture'])
    else:
        w.u16(m['texture'])
    for c in m['colors']: w.u16(c)
    w.u16(m['w24']); w.u16(m['w26'])
    if ver < 6: w.u16(m['flagword'])
    w.u16(m['w2c'])
    for tex, v in m['maps']:
        w.cstr(tex) if ver >= 6 else w.u16(tex)
        w.u16(v)
    if ver >= 6:
        for tex, v in m['extra']:
            w.cstr(tex); w.u16(v)


def read_lod(r):
    lod = {'value': r.i32(), 'flags': r.u32()}
    if r.peek() == 'BONE':
        r.tag(); lod['bones'] = [r.cstr() for _ in range(r.count())]; r.expect('/BON')
    r.expect('POIN')
    pts = []
    d = r.d
    for _ in range(r.count()):
        r.need(2)
        f = (d[r.o] << 8) | d[r.o + 1]; r.o += 2
        s = point_struct(f)
        r.need(s.size)
        pts.append((f,) + s.unpack_from(d, r.o)); r.o += s.size
    lod['points'] = pts
    r.expect('/POI')
    if r.peek() == 'WEIG':
        r.tag(); n = r.count()
        if n != len(pts):
            raise FormatError(f'WEIG count {n} != point count {len(pts)}')
        lod['weights'] = [[(r.u16(), r.i32()) for _ in range(r.u16())] for _ in range(n)]
        r.expect('/WEI')
    r.expect('PART')
    parts = []
    for _ in range(r.count()):
        part = {'flags': r.u32(), 'groups': []}
        pre = part['flags'] & PART_PRECOMPUTED
        for _ in range(r.u16()):
            g = {'material': r.i32()}
            n = r.count(); flat = r.unpack('>%di' % (4 * n))
            g['faces'] = [flat[i:i + 4] for i in range(0, 4 * n, 4)]
            if pre:
                n = r.count(); flat = r.unpack('>%di' % (7 * n))
                g['extra'] = [flat[i:i + 7] for i in range(0, 7 * n, 7)]
            part['groups'].append(g)
        if pre:
            part['bounds'] = r.ints(10)
        parts.append(part)
    lod['parts'] = parts
    r.expect('/PAR')
    return lod


def write_lod(w, lod):
    w.i32(lod['value']); w.u32(lod['flags'])
    if 'bones' in lod:
        w.tag('BONE'); w.i32(len(lod['bones']))
        for b in lod['bones']: w.cstr(b)
        w.tag('/BON')
    w.tag('POIN'); w.i32(len(lod['points']))
    b = w.b
    for p in lod['points']:
        b += struct.pack('>H', p[0]) + point_struct(p[0]).pack(*p[1:])
    w.tag('/POI')
    if 'weights' in lod:
        if len(lod['weights']) != len(lod['points']):
            raise FormatError('WEIG count != point count')
        w.tag('WEIG'); w.i32(len(lod['weights']))
        for ws in lod['weights']:
            w.u16(len(ws))
            for bone, weight in ws: w.u16(bone); w.i32(weight)
        w.tag('/WEI')
    w.tag('PART'); w.i32(len(lod['parts']))
    for part in lod['parts']:
        pre = part['flags'] & PART_PRECOMPUTED
        w.u32(part['flags']); w.u16(len(part['groups']))
        for g in part['groups']:
            w.i32(g['material']); w.i32(len(g['faces']))
            w.ints([v for f in g['faces'] for v in f])
            if pre:
                w.i32(len(g['extra']))
                w.ints([v for e in g['extra'] for v in e])
        if pre:
            w.ints(part['bounds'])
    w.tag('/PAR')


def kind(data):
    """'BOB1', 'CUT1' (scene container, table 0x0054eed0; not a body) or None."""
    head = bytes(data[:4])
    return {b'BOB1': 'BOB1', b'CUT1': 'CUT1'}.get(head)


def parse(data):
    """Parse a decoded BOB1 body; raises FormatError on any deviation the engine rejects."""
    if kind(data) != 'BOB1':
        raise FormatError(f'not a BOB1 body (magic {bytes(data[:4])!r})')
    r = Reader(data); r.expect('BOB1')
    sections = []
    while True:
        t = r.tag()
        if t == '/BOB':
            break
        if t in ('INFO', 'NAME'):
            sections.append((t, r.cstr()))
        elif t in ('VERS', 'SND1'):
            sections.append((t, r.u32()))
        elif t in MATVER:
            ver = MATVER[t]
            sections.append((t, [read_material(r, ver) for _ in range(r.count())]))
        elif t == 'BODY':
            sections.append((t, [read_lod(r) for _ in range(r.u16())]))
        else:
            raise FormatError(f'unsupported or unknown tag {t!r} at {r.o - 4:#x}')
        r.expect('/' + t[:3])
    if r.o != len(data):
        raise FormatError(f'{len(data) - r.o} trailing bytes after /BOB')
    return {'sections': sections}


def serialise(tree):
    w = Writer(); w.tag('BOB1')
    for t, v in tree['sections']:
        w.tag(t)
        if t in ('INFO', 'NAME'):
            w.cstr(v)
        elif t in ('VERS', 'SND1'):
            w.u32(v)
        elif t in MATVER:
            w.i32(len(v))
            for m in v: write_material(w, m, MATVER[t])
        elif t == 'BODY':
            if len(v) > 0xffff:
                raise FormatError('too many LOD records')
            w.u16(len(v))
            for lod in v: write_lod(w, lod)
        else:
            raise FormatError(f'cannot write section {t!r}')
        w.tag('/' + t[:3])
    w.tag('/BOB')
    return bytes(w.b)


def lods(tree):
    for t, v in tree['sections']:
        if t == 'BODY':
            return v
    raise FormatError('body has no BODY section')


def materials(tree):
    for t, v in tree['sections']:
        if t in MATVER:
            return v
    return []


def lod_summary(lod):
    groups = [len(p['groups']) for p in lod['parts']]
    mats = [g['material'] for p in lod['parts'] for g in p['groups']]
    return dict(points=len(lod['points']), parts=len(lod['parts']), groups_per_part=groups,
                draws=sum(groups), faces=sum(len(g['faces']) for p in lod['parts'] for g in p['groups']),
                materials=sorted(set(mats)), part_flags=sorted({p['flags'] for p in lod['parts']}))


def format_ladder(tree, out=None):
    out = out or sys.stdout
    ls = lods(tree)
    print(f'LODs {len(ls)}  materials {len(materials(tree))}', file=out)
    for i, lod in enumerate(ls):
        s = lod_summary(lod)
        th = f'scale={lod["value"]}' if i == 0 else f'threshold={lod["value"]}'
        mats = s['materials'] if len(s['materials']) <= 12 else \
            s['materials'][:12] + [f'... {len(s["materials"])} distinct']
        print(f'  LOD{i} {th} flags={lod["flags"]:#x} points={s["points"]} parts={s["parts"]}'
              f' groups/part={s["groups_per_part"]} draws={s["draws"]} faces={s["faces"]}'
              f' part_flags={[hex(f) for f in s["part_flags"]]} materials={mats}', file=out)


# --- archive access (read-only) -------------------------------------------

def _archive_modules():
    here = str(Path(__file__).resolve().parent)
    if here not in sys.path:
        sys.path.insert(0, here)
    import sector_fog_census
    return sector_fog_census


def body_stem(name):
    """Scene reference ('stations\\docks\\argon_dock_center') or member path -> stem under objects/."""
    stem = name.replace('\\', '/').strip('/')
    low = stem.lower()
    for ext in BODY_EXTENSIONS:
        if low.endswith(ext):
            stem = stem[:-len(ext)]; low = low[:-len(ext)]
            break
    if low.startswith('addon/'):
        stem = stem[6:]; low = low[6:]
    if not low.startswith('objects/'):
        stem = 'objects/' + stem
    return stem


def resolve_body(assets, name):
    """Winning archive entry for a body name under the Assets overlay precedence."""
    stem = body_stem(name)
    found = [(ext, assets.candidates(stem + ext)) for ext in ('.pbb', '.pbd')]  # .bob/.bod alias these
    found = [(ext, entries) for ext, entries in found if entries]
    if not found:
        raise FileNotFoundError(f'no body resource for {stem}')
    if len(found) > 1:
        raise FormatError(f'both binary and text bodies exist for {stem}; engine order unverified')
    return found[0][1][-1]


def load(target, game=DEFAULT_GAME):
    """(decoded bytes, provenance string) for a file path or an archive body name."""
    p = Path(target)
    if p.is_file():
        sfc = _archive_modules()
        return sfc.unpack(p.read_bytes()), str(p)
    sfc = _archive_modules()
    assets = sfc.Assets(Path(game))
    entry = resolve_body(assets, target)
    return assets.read_entry(entry), f'{entry["source"]}:{entry["path"]}'


VIEW_DISTANCE = {'low': 0, 'medium': 1, 'high': 2, 'very-high': 3}   # cfg+0x768 (VideoViewDistance)


def reachable(thresholds, f=1.0):
    """Loop-reachable indices of 0047d429..0047d46e (lod-selection.md, 2026-09-23 section).

    thresholds = file values of records 1..n-1. The loop takes the highest i with
    s < trunc(T_i*f), s >= 1, so i >= 1 is reachable iff trunc(T_i*f) >= 2 and it
    exceeds trunc(T_j*f) of every later record j. Record 0 is the no-hit result."""
    t = [int(x * f) for x in thresholds]           # ftol truncation (0x0052b5d0)
    return [0] + [i + 1 for i in range(len(t)) if t[i] >= 2 and all(t[i] > u for u in t[i + 1:])]


def drawable(thresholds, view='very-high', f=1.0):
    """Records drawable in the main view (no 0x1000000 flag) at a View Distance setting:
    final = clamp(sel - 1, 0, n-1) at Very High, sel at Low..High. Low's adaptive
    rescale of small metrics (only ever coarser) is not modelled."""
    n = len(thresholds) + 1
    r = reachable(thresholds, f)
    if VIEW_DISTANCE[view] >= 3:
        r = [min(max(i - 1, 0), n - 1) for i in r]
    return sorted(set(r))


def final_index(thresholds, s, view='very-high', f=1.0):
    """Main-view record drawn for metric s >= 1: sel = highest i with s < trunc(T_i*f)
    walking from the last record (record 0 never compared), then -1 at Very High, clamped."""
    t = [int(x * f) for x in thresholds]
    sel = next((i for i in range(len(t), 0, -1) if s < t[i - 1]), 0)
    return min(max(sel - 1, 0), len(t)) if VIEW_DISTANCE[view] >= 3 else sel


def selection_bands(thresholds, view='very-high', f=1.0):
    """[(lo, hi, record)]: s in [lo, hi) draws record in the main view (hi None = unbounded).
    Exact: the drawn index only changes where s crosses some trunc(T_i*f)."""
    cuts = sorted({1} | {int(x * f) for x in thresholds if int(x * f) > 1})
    bands = []
    for lo, hi in zip(cuts, cuts[1:] + [None]):
        k = final_index(thresholds, lo, view, f)
        if bands and bands[-1][2] == k:
            bands[-1] = (bands[-1][0], hi, k)
        else:
            bands.append((lo, hi, k))
    return bands


def format_bands(bands):
    return ' '.join(f's<{hi}:LOD{k}' if lo == 1 and hi is not None else
                    (f's>={lo}:LOD{k}' if hi is None else f'{lo}<=s<{hi}:LOD{k}') for lo, hi, k in bands)


def audit_row(tree, view='very-high', f=1.0):
    ladder = lods(tree)
    draws = [lod_summary(l)['draws'] for l in ladder]
    th = [l['value'] for l in ladder[1:]]
    n = len(ladder)
    drawn = drawable(th, view, f)
    anywhere = set(drawable(th, 'high', f)) | set(drawable(th, 'very-high', f))
    return dict(lods=n, thresholds=th, groups=draws, drawable=drawn,
                never_drawn=[i for i in range(n) if i not in drawn],
                never_drawn_any_setting=[i for i in range(n) if i not in anywhere],
                single_lod=n == 1, last_never_drawn=n > 1 and (n - 1) not in drawn,
                lod0_only=n > 1 and drawn == [0], dead_any_setting=n > 1 and len(anywhere) < n,
                coarse_multi_group=draws[max(drawn)] > 1)


AUDIT_CLASSES = ('single_lod', 'last_never_drawn', 'lod0_only', 'dead_any_setting', 'coarse_multi_group')
AUDIT_LEGEND = ('classes (overlap): single_lod = one record; last_never_drawn = the last record is not in'
                ' the main-view drawable set at this setting; lod0_only = multi-LOD body that can only draw'
                ' LOD 0 at this setting; dead_any_setting = some record drawable at no main-view setting'
                ' (Low..High or Very High); coarse_multi_group = the coarsest drawable record draws > 1 group')


def audit(assets, emit=None, view='very-high', f=1.0):
    """Audit every winning .pbb resource; returns (rows, summary dict)."""
    keys = sorted(k for k, v in assets.entries.items() if v[-1]['path'].lower().endswith('.pbb'))
    rows = []
    summary = dict(view_distance=view, factor=f, pbb_resources=len(keys), cut1=0, bob1=0, rejected=0,
                   multi_lod=0, **{c: 0 for c in AUDIT_CLASSES}, coarse_multi_group_multi_lod=0,
                   never_drawn_records=0, dead_any_setting_records=0)
    for key in keys:
        entry = assets.entries[key][-1]
        data = assets.read_entry(entry)
        assets.cache.clear()
        member = f'{entry["source"]}:{entry["path"]}'
        k = kind(data)
        if k == 'CUT1':
            summary['cut1'] += 1
            continue
        summary['bob1'] += k == 'BOB1'
        try:
            row = dict(member=member, **audit_row(parse(data), view, f))
        except FormatError as exc:
            summary['rejected'] += 1
            row = dict(member=member, error=str(exc))
        else:
            for c in AUDIT_CLASSES:
                summary[c] += row[c]
            summary['multi_lod'] += row['lods'] > 1
            summary['coarse_multi_group_multi_lod'] += row['lods'] > 1 and row['coarse_multi_group']
            summary['never_drawn_records'] += len(row['never_drawn'])
            summary['dead_any_setting_records'] += len(row['never_drawn_any_setting'])
        rows.append(row)
        if emit:
            emit(row)
    return rows, summary


def format_audit_row(row):
    if 'error' in row:
        return f'{row["member"]} REJECTED {row["error"]}'
    flags = [c for c in AUDIT_CLASSES if row[c]]
    dead = f' dead_any_setting={row["never_drawn_any_setting"]}' if row['never_drawn_any_setting'] else ''
    return (f'{row["member"]} lods={row["lods"]} thresholds={row["thresholds"]} groups={row["groups"]}'
            f' drawable={row["drawable"]} {",".join(flags) or "ok"}{dead}')


def main(argv=None):
    ap = argparse.ArgumentParser(description='BOB1 body inspector (prints numbers only).')
    sub = ap.add_subparsers(dest='cmd', required=True)
    info = sub.add_parser('info', help='print the LOD ladder of a body')
    info.add_argument('body', help='file path, archive member path or scene body name')
    info.add_argument('--game', type=Path, default=DEFAULT_GAME)
    au = sub.add_parser('audit', help='LOD ladder classes of every installed BOB1 body')
    au.add_argument('--game', type=Path, default=DEFAULT_GAME)
    au.add_argument('--json', type=Path, help='write the full per-body table as JSON')
    au.add_argument('--summary', action='store_true', help='print only the summary lines')
    au.add_argument('--view-distance', choices=list(VIEW_DISTANCE), default='very-high',
                    help='VideoViewDistance setting for the drawable sets (default: very-high, the X3 bottle)')
    au.add_argument('--factor', type=float, default=1.0,
                    help='threshold multiplier f = cfg+0x760 (engine default 1.0; --lod-scale changes it)')
    a = ap.parse_args(argv)
    if a.cmd == 'audit':
        game = a.game.resolve()
        if a.json and a.json.resolve().is_relative_to(game):
            raise SystemExit('--json must be outside the game directory')
        emit = None if a.summary else (lambda row: print(format_audit_row(row)))
        rows, summary = audit(_archive_modules().Assets(game), emit, a.view_distance, a.factor)
        print('summary ' + ' '.join(f'{k}={v}' for k, v in summary.items()))
        print(AUDIT_LEGEND)
        if a.json:
            import json
            a.json.write_text(json.dumps(dict(summary=summary, bodies=rows), indent=1) + '\n')
        return 0
    data, origin = load(a.body, a.game)
    k = kind(data)
    print(f'{origin}  decoded_bytes={len(data)}  magic={k or bytes(data[:4])!r}')
    if k == 'CUT1':
        print('CUT1 scene container: not a body, skipped')
        return 0
    tree = parse(data)
    print(f'sections {[t for t, _ in tree["sections"]]}  roundtrip_equal={serialise(tree) == data}')
    format_ladder(tree)
    return 0


def cli():
    try:
        return main()
    except (FileNotFoundError, FormatError) as exc:
        print(f'error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(cli())
