#!/usr/bin/env python3
"""Parse and re-serialise every installed BOB1 body (.pbb) with the layout decoded
from X3AP.exe 0x00481aa0 (docs/reverse-engineering/body-format-bob1.md).

Read-only: archives are opened through tools/analysis/sector_fog_census.Assets;
no body bytes are written anywhere. Prints only counts, derived statistics and
the first records of named sample bodies.

Acceptance: for every parsed member, serialise(parse(data)) == data (byte
equality) and the parse consumes the buffer exactly up to the final '/BOB'.
Usage: PYTHONPATH=tools/analysis python3 verification/results/bob1-format/bob1_roundtrip.py [--limit N]
"""
import argparse
import os
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
from sector_fog_census import Assets  # noqa: E402

ROOT = Path(os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'))
PART_PRECOMPUTED = 0x10000000          # part flag: per-group extra records + 10 precomputed ints


class R:
    def __init__(self, d):
        self.d, self.o = d, 0

    def tag(self):
        t = self.d[self.o:self.o + 4]; self.o += 4; return t.decode('latin1')

    def expect(self, t):
        got = self.tag()
        if got != t:
            raise ValueError(f'expected {t} got {got!r} at {self.o - 4:#x}')

    def peek(self):
        return self.d[self.o:self.o + 4].decode('latin1')

    def u16(self):
        v = struct.unpack_from('>H', self.d, self.o)[0]; self.o += 2; return v

    def i32(self):
        v = struct.unpack_from('>i', self.d, self.o)[0]; self.o += 4; return v

    def u32(self):
        v = struct.unpack_from('>I', self.d, self.o)[0]; self.o += 4; return v

    def ints(self, n):
        v = struct.unpack_from('>%di' % n, self.d, self.o); self.o += 4 * n; return list(v)

    def cstr(self):
        e = self.d.index(b'\0', self.o); s = self.d[self.o:e]; self.o = e + 1; return s


class W:
    def __init__(self):
        self.b = bytearray()

    def tag(self, t): self.b += t.encode('latin1')
    def u16(self, v): self.b += struct.pack('>H', v)
    def i32(self, v): self.b += struct.pack('>i', v)
    def u32(self, v): self.b += struct.pack('>I', v)
    def ints(self, v): self.b += struct.pack('>%di' % len(v), *v)
    def cstr(self, s): self.b += s + b'\0'


# SPTYPE_* value sizes in 32-bit words (0x00470490; table 0x0054f0a0); 8 = string
SPTYPE_WORDS = {0: 1, 1: 1, 2: 1, 3: 2, 4: 3, 5: 4, 6: 9, 7: 16}


def read_material(r, ver):
    m = {'index': r.u16()}
    if ver >= 6:
        m['flags'] = r.u32()
        if m['flags'] & 0x2000000:          # effect material, 0x00470490
            m['technique'] = r.u16(); m['effect'] = r.cstr(); params = []
            for _ in range(r.u16()):
                name = r.cstr(); typ = r.u16()
                val = r.cstr() if typ == 8 else r.ints(SPTYPE_WORDS[typ])
                params.append((name, typ, val))
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
    # three (map texture, value) pairs at +0x2e/+0x30, +0x32/+0x34, +0x36/+0x38
    m['maps'] = []
    for _ in range(3):
        tex = r.cstr() if ver >= 6 else r.u16()
        m['maps'].append((tex, r.u16()))
    if ver >= 6:
        m['extra'] = [(r.cstr(), r.u16()), (r.cstr(), r.u16())]   # +0x1c/+0x1e, +0x20/+0x22
    return m


def write_material(w, m, ver):
    w.u16(m['index'])
    if ver >= 6:
        w.u32(m['flags'])
        if m['flags'] & 0x2000000:
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
        w.cstr(tex) if ver >= 6 else w.u16(tex); w.u16(v)
    if ver >= 6:
        for tex, v in m['extra']: w.cstr(tex); w.u16(v)


def read_lod(r, first):
    lod = {'value': r.i32(), 'flags': r.u32()}     # LOD0: size -> +0x14; else threshold -> +0x34
    if r.peek() == 'BONE':
        r.tag(); lod['bones'] = [r.cstr() for _ in range(r.i32())]; r.expect('/BON')
    r.expect('POIN'); pts = []
    for _ in range(r.i32()):
        f = r.u16(); p = [f]
        if f & 1: p.append(r.ints(3))                       # position (file units; engine stores >>2 as int16)
        if f & 2:
            p.append(r.ints(2))                             # uv 16.16
            if f & 4: p.append(r.ints(2))                   # second uv set 16.16 -> LOD+0x0c
        if f & 8: p.append(r.ints(3))                       # normal 16.16 (engine stores >>2 as int16)
        if f & 0x10: p.append(r.u32())                      # u32 -> point+0x14
        pts.append(p)
    lod['points'] = pts; r.expect('/POI')
    if r.peek() == 'WEIG':
        r.tag(); n = r.i32()
        if n != len(pts): raise ValueError('WEIG count != point count')
        ws = []
        for _ in range(n):
            ws.append([(r.u16(), r.i32()) for _ in range(r.u16())])
        lod['weights'] = ws; r.expect('/WEI')
    r.expect('PART'); parts = []
    for _ in range(r.i32()):
        part = {'flags': r.u32(), 'groups': []}
        for _ in range(r.u16()):
            g = {'material': r.i32()}
            g['faces'] = [r.ints(4) for _ in range(r.i32())]  # i0,i1,i2, face word (stored int16 at +0xc)
            if part['flags'] & PART_PRECOMPUTED:
                g['extra'] = [r.ints(7) for _ in range(r.i32())]
            part['groups'].append(g)
        if part['flags'] & PART_PRECOMPUTED:
            part['bounds'] = r.ints(10)
        parts.append(part)
    lod['parts'] = parts; r.expect('/PAR')
    return lod


def write_lod(w, lod):
    w.i32(lod['value']); w.u32(lod['flags'])
    if 'bones' in lod:
        w.tag('BONE'); w.i32(len(lod['bones']))
        for b in lod['bones']: w.cstr(b)
        w.tag('/BON')
    w.tag('POIN'); w.i32(len(lod['points']))
    for p in lod['points']:
        f = p[0]; w.u16(f); rest = iter(p[1:])
        if f & 1: w.ints(next(rest))
        if f & 2:
            w.ints(next(rest))
            if f & 4: w.ints(next(rest))
        if f & 8: w.ints(next(rest))
        if f & 0x10: w.u32(next(rest))
    w.tag('/POI')
    if 'weights' in lod:
        w.tag('WEIG'); w.i32(len(lod['weights']))
        for ws in lod['weights']:
            w.u16(len(ws))
            for bone, weight in ws: w.u16(bone); w.i32(weight)
        w.tag('/WEI')
    w.tag('PART'); w.i32(len(lod['parts']))
    for part in lod['parts']:
        w.u32(part['flags']); w.u16(len(part['groups']))
        for g in part['groups']:
            w.i32(g['material']); w.i32(len(g['faces']))
            for f in g['faces']: w.ints(f)
            if part['flags'] & PART_PRECOMPUTED:
                w.i32(len(g['extra']))
                for e in g['extra']: w.ints(e)
        if part['flags'] & PART_PRECOMPUTED: w.ints(part['bounds'])
    w.tag('/PAR')


MATVER = {'MAT3': 3, 'MAT5': 5, 'MAT6': 6}


def parse(data):
    r = R(data); r.expect('BOB1'); sections = []
    while True:
        t = r.tag()
        if t == '/BOB': break
        if t in ('INFO', 'NAME'):
            sections.append((t, r.cstr()))
        elif t in ('VERS', 'SND1'):
            sections.append((t, r.u32()))
        elif t in MATVER:
            ver = MATVER[t]
            sections.append((t, [read_material(r, ver) for _ in range(r.i32())]))
            t = 'MAT'
        elif t == 'BODY':
            n = r.u16(); sections.append((t, [read_lod(r, i == 0) for i in range(n)]))
        else:
            raise ValueError(f'unsupported tag {t!r} at {r.o - 4:#x}')
        r.expect('/' + t[:3])
    if r.o != len(data):
        raise ValueError(f'{len(data) - r.o} trailing bytes')
    return sections


def serialise(sections):
    w = W(); w.tag('BOB1')
    for t, v in sections:
        w.tag(t)
        if t in ('INFO', 'NAME'): w.cstr(v)
        elif t in ('VERS', 'SND1'): w.u32(v)
        elif t in MATVER:
            w.i32(len(v))
            for m in v: write_material(w, m, MATVER[t])
            t = 'MAT'
        elif t == 'BODY':
            w.u16(len(v))
            for lod in v: write_lod(w, lod)
        w.tag('/' + t[:3])
    w.tag('/BOB')
    return bytes(w.b)


SAMPLES = ('objects/stations/x3tc/torus_backgroundtraffic_dots.pbb',
           'objects/stations/x3ap/others/xtc_teladi_eqd_ring1b.pbb',
           'objects/stations/station_scenes/others/argon_L_solarpowerplant.pbb')


def describe(path, secs):
    tags = [t for t, _ in secs]
    print(f'SAMPLE {path} tags={tags}')
    for t, v in secs:
        if t in MATVER:
            eff = sum(1 for m in v if m.get('flags', 0) & 0x2000000)
            print(f'  {t} materials={len(v)} effect_materials={eff} first_index={v[0]["index"]}'
                  f' first_flags={v[0].get("flags", 0):#x} first_effect={v[0].get("effect", b"").decode()}'
                  f' first_params={len(v[0].get("params", []))}')
        if t == 'BODY':
            for i, lod in enumerate(v):
                groups = sum(len(p['groups']) for p in lod['parts'])
                faces = sum(len(g['faces']) for p in lod['parts'] for g in p['groups'])
                pf = Counter(p[0] for p in lod['points'])
                mats = sorted({g['material'] for p in lod['parts'] for g in p['groups']})
                print(f'  LOD{i} value={lod["value"]} flags={lod["flags"]:#x} points={len(lod["points"])}'
                      f' point_flags={dict(pf)} parts={len(lod["parts"])} groups(subsets)={groups}'
                      f' faces={faces} distinct_materials={len(mats)} bones={len(lod.get("bones", []))}'
                      f' weights={"yes" if "weights" in lod else "no"}'
                      f' part_flags={sorted({hex(p["flags"]) for p in lod["parts"]})}')
                if i == 0:
                    print(f'    first_point={lod["points"][0]}')
                    g = lod['parts'][0]['groups'][0]
                    print(f'    first_group material={g["material"]} faces={len(g["faces"])} first_face={g["faces"][0]}'
                          f' extra={len(g.get("extra", []))} first_extra={g.get("extra", [None])[0]}')
                    print(f'    first_part_bounds={lod["parts"][0].get("bounds")}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    assets = Assets(ROOT)
    keys = sorted(k for k, v in assets.entries.items() if v[-1]['path'].lower().endswith('.pbb'))
    if a.limit: keys = keys[:a.limit]
    ok = fail = 0; failures = Counter(); stats = Counter(); lodcount = Counter()
    magic = Counter(); pflags = Counter(); partflags = Counter(); tagseq = Counter(); matver = Counter()
    coarse_fewer = coarse_same = coarse_more = 0; thresholds_desc = thresholds_other = 0
    for k in keys:
        entry = assets.entries[k][-1]
        data = assets.read_entry(entry)
        magic[data[:4]] += 1
        if data[:4] != b'BOB1':
            continue                       # CUT1 scene containers use the 0x0054eed0 table, not this parser
        try:
            secs = parse(data)
            if serialise(secs) != data:
                raise ValueError('round-trip mismatch')
        except Exception as e:  # noqa: BLE001
            fail += 1; failures[str(e).split(' at ')[0][:60] + ' | ' + entry['source'] + ':' + entry['path']] += 1
            continue
        ok += 1
        tagseq[' '.join(t for t, _ in secs)] += 1
        for t, v in secs:
            if t in MATVER: matver[t] += 1
            if t == 'BODY':
                lodcount[len(v)] += 1
                subsets = [sum(len(p['groups']) for p in lod['parts']) for lod in v]
                for lod in v:
                    for p in lod['points']: pflags[p[0]] += 1
                    for p in lod['parts']: partflags[p['flags']] += 1
                    stats['bones'] += 'bones' in lod; stats['weights'] += 'weights' in lod
                    stats['lods'] += 1
                if len(v) > 1:
                    if subsets[-1] < subsets[0]: coarse_fewer += 1
                    elif subsets[-1] == subsets[0]: coarse_same += 1
                    else: coarse_more += 1
                    th = [lod['value'] for lod in v[1:]]
                    if all(x > y for x, y in zip(th, th[1:])): thresholds_desc += 1
                    else: thresholds_other += 1
        if k.lower() in {s.lower().replace('.pbb', '.bob') for s in SAMPLES} or \
           entry['path'].replace('\\', '/').lower() in {s.lower() for s in SAMPLES}:
            describe(entry['path'], secs)
    print(f'members={len(keys)} magic={ {k.decode("latin1"): v for k, v in magic.items()} }'
          f' BOB1_roundtrip_equal={ok} BOB1_failed={fail}')
    for f, n in failures.most_common(): print(f'  failure {n}: {f}')
    print('section sequences:', dict(tagseq.most_common(8)))
    print('material tags:', dict(matver))
    print('LOD count histogram:', dict(sorted(lodcount.items())))
    print('LOD records:', stats['lods'], 'with BONE:', stats['bones'], 'with WEIG:', stats['weights'])
    print('point flag histogram:', {hex(k): v for k, v in pflags.most_common()})
    print('part flag histogram:', {hex(k): v for k, v in partflags.most_common(10)})
    print(f'multi-LOD bodies: coarsest has fewer subsets={coarse_fewer} same={coarse_same} more={coarse_more};'
          f' thresholds strictly decreasing={thresholds_desc} other={thresholds_other}')


if __name__ == '__main__':
    main()
