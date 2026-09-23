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
import math
import os
import re
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


def parse(data, max_trailing=0):
    """Parse a decoded body, dispatching on the magic as 0x004863c0 does: a payload starting
    with 'BOB' is binary (parse_binary), anything else is the text form (parse_text). A CUT1
    scene container is refused (not a body)."""
    head = bytes(data[:4])
    if head == b'CUT1' or head.startswith(b'BOB'):
        return parse_binary(data, max_trailing)
    return parse_text(data)


def parse_binary(data, max_trailing=0):
    """Parse a decoded BOB1 body; raises FormatError on any deviation the engine rejects.

    Up to max_trailing bytes after the final /BOB are tolerated and counted in the tree's
    'trailing_bytes' (serialise never writes them): the engine parser 0x00481aa0 returns the
    model at the /BOB closer and never reads past it (body-format-bob1.md section 1), so such
    bytes are inert; mod tooling leaves 1-2 stray closer bytes on 86 bodies of the tested mods."""
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
    tree = {'sections': sections}
    if r.o != len(data):
        n = len(data) - r.o
        if n > max_trailing:
            raise FormatError(f'{n} trailing bytes after /BOB')
        tree['trailing_bytes'] = n
    return tree


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


# --- text form (.bod / .pbd) --------------------------------------------------------------
# Grammar derived from the shipped text bodies and their compiled twins
# (body-format-bob1.md section 8, "Text form"); the engine's text parser 0x00483f20 is not decompiled.

TEXT_POS_DIVISOR = 1.52587890625        # BOD position unit = BOB unit * 100000/65536 (x2bc)
SPTYPE_NAMES = {'SPTYPE_LONG': 0, 'SPTYPE_BOOL': 1, 'SPTYPE_FLOAT': 2, 'SPTYPE_FLOAT2': 3, 'SPTYPE_FLOAT3': 4,
                'SPTYPE_FLOAT4': 5, 'SPTYPE_MATRIX3': 6, 'SPTYPE_MATRIX4': 7, 'SPTYPE_STRING': 8}
FACE_BASE, FACE_UV, FACE_SMOOTH = 1, 8, 16          # face flag = -(bits)
TEXT_MATERIALS = {'MATERIAL3': 'MAT3', 'MATERIAL5': 'MAT5', 'MATERIAL6': 'MAT6'}
_TEXT_LEX = re.compile(r'/!([^\n]*?)!/|/[^\n]*')      # /! block !/ on one line, else '/' comment to end of line
_TEXT_INT = re.compile(r'-?[0-9]+')
_TEXT_HEX = re.compile(r'0[xX][0-9a-fA-F]+')
_TEXT_NUM = re.compile(r'-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?')
I32, U32 = (-0x80000000, 0x7fffffff), (0, 0xffffffff)
_SCENE_LINE = re.compile(rb'^[ \t]*(VER[ \t]*:|P[ \t]+-?\d+[ \t]*;)', re.M)


def text_kind(data):
    """'scene' for a text scene (VER:/P n; node lines, the text twin of CUT1), else 'body'."""
    return 'scene' if _SCENE_LINE.search(bytes(data[:65536])) else 'body'


class TextReader:
    """Field stream of a text body: ';'-separated values with '//' and '/' comments removed;
    a /! ... !/ block is one magic field ('\\0', content). Tracks the line for errors."""

    def __init__(self, data):
        data = bytes(data)
        text = (data[3:] if data.startswith(b'\xef\xbb\xbf') else data).decode('latin1')
        self.magic, self.info = [], None

        def lex(m):
            if m.group(1) is not None:
                self.magic.append(m.group(1))
                return f'\0{len(self.magic) - 1};'
            if m.group(0).startswith('/!'):
                raise FormatError(f'text line {text.count(chr(10), 0, m.start()) + 1}: unclosed /! block')
            if self.info is None and m.group(0).startswith('/#'):
                self.info = m.group(0)[2:].strip()
            return ''
        self.fields = self._split(_TEXT_LEX.sub(lex, text))
        self.i = 0

    @staticmethod
    def _split(code):
        out, pending, line = [], '', 1
        for n, chunk in enumerate(code.split('\n'), 1):
            parts = chunk.split(';')
            parts[0] = pending + '\n' + parts[0] if pending.strip() else parts[0]
            for p in parts[:-1]:
                out.append((p.strip(), n))
            pending = parts[-1]
            line = n
        if pending.strip():
            raise FormatError(f'text: unterminated value {pending.strip()[:40]!r} at line {line}')
        return out

    def error(self, msg):
        line = self.fields[min(self.i, len(self.fields) - 1)][1] if self.fields else 0
        return FormatError(f'text line {line}: {msg}')

    def done(self):
        return self.i >= len(self.fields)

    def peek(self):
        return self.fields[self.i][0] if self.i < len(self.fields) else None

    def next(self):
        if self.i >= len(self.fields):
            raise FormatError('text: truncated (unexpected end of body)')
        v = self.fields[self.i][0]; self.i += 1
        return v

    def is_magic(self):
        v = self.peek()
        return v is not None and v.startswith('\0')

    def peek_magic(self):
        v = self.peek()
        return self.magic[int(v[1:])] if v is not None and v.startswith('\0') else None

    def magic_field(self):
        v = self.next()
        if not v.startswith('\0'):
            self.i -= 1
            raise self.error(f'expected a /! !/ block, got {v[:40]!r}')
        return self.magic[int(v[1:])]

    def int(self, what='integer', span=I32):
        v = self.next()
        if not _TEXT_INT.fullmatch(v):
            self.i -= 1
            raise self.error(f'expected {what}, got {v[:40]!r}')
        n = int(v)
        if not span[0] <= n <= span[1]:
            self.i -= 1
            raise self.error(f'{what} {v[:40]} outside {span[0]}..{span[1]}')
        return n

    def num(self, what='number'):
        v = self.next()
        n = float(v) if _TEXT_NUM.fullmatch(v) else math.nan
        if not math.isfinite(n):
            self.i -= 1
            raise self.error(f'expected a finite {what}, got {v[:40]!r}')
        return n

    def string(self, what='string'):
        v = self.next()
        if '\0' in v:
            self.i -= 1
            raise self.error(f'expected {what}, got a /! block')
        return v.encode('latin1')

    def bits(self, what):
        """A flags value written as a binary digit string (0000000001000000 = 0x40), 32 bits at most."""
        v = self.next()
        if not v or set(v) - {'0', '1'} or int(v, 2) > 0xffffffff:
            self.i -= 1
            raise self.error(f'{what} {v[:40]!r} is not a binary digit string of at most 32 bits')
        return int(v, 2)

    def word(self, what='16-bit value'):
        v = self.int(what)
        if not 0 <= v <= 0xffff:
            self.i -= 1
            raise self.error(f'{what} {v} outside 0..65535')
        return v


def _fixed(v):
    """16.16 fixed point, rounded to nearest (x2bc); must fit a signed 32-bit int."""
    if not math.isfinite(v):
        raise FormatError(f'text: {v} is not finite')
    n = int(math.floor(v * 65536.0 + 0.5))
    if not I32[0] <= n <= I32[1]:
        raise FormatError(f'text: {v} outside the 16.16 range')
    return n


def _text_int(text, what, span=I32):
    if _TEXT_HEX.fullmatch(text):
        n = int(text, 16)
    elif _TEXT_INT.fullmatch(text):
        n = int(text)
    else:
        raise FormatError(f'text: bad {what} {text[:40]!r}')
    if not span[0] <= n <= span[1]:
        raise FormatError(f'text: {what} {text[:40]} outside {span[0]}..{span[1]}')
    return n


def _text_name(v):
    return b'' if v.upper() == b'NULL' else v


def _read_text_material(r, key, first):
    """One MATERIALn record; `first` is the value after 'MATERIALn:' (the index)."""
    ver = MATVER[TEXT_MATERIALS[key]]
    m = {'index': _text_int(first, 'material index', (0, 0xffff))}
    if ver >= 6:
        m['flags'] = _text_int(r.next(), 'material flags', U32)
        if m['flags'] & EFFECT_MATERIAL:
            m['technique'] = r.word('technique')
            m['effect'] = r.string('effect file')
            params = []
            for _ in range(r.word('parameter count')):
                name = r.string('parameter name')
                tname = r.next()
                typ = SPTYPE_NAMES.get(tname.upper())
                if typ is None:
                    raise r.error(f'unknown effect parameter type {tname[:40]!r}')
                if typ == 8:
                    val = r.string('parameter value')
                elif typ in (0, 1):
                    val = [r.int('parameter value')]
                else:
                    val = [_fixed(r.num('parameter value')) for _ in range(SPTYPE_WORDS[typ])]
                params.append((name, typ, val))
            m['params'] = params
            return m
        tex = r.string('texture')
        if _TEXT_INT.fullmatch(tex.decode('latin1')):
            raise r.error('MATERIAL6 with a numeric texture (MATERIAL3 layout) is not supported')
        m['texture'] = _text_name(tex)
    else:
        m['texture'] = r.word('texture id')
    colors = [r.word('colour') for _ in range(9)]
    transparency = r.int('transparency', (I32[0], U32[1])) & 0xffffffff
    m['colors'] = colors + [transparency >> 16, transparency & 0xffff, r.word('self illumination')]
    m['w24'] = r.word('shininess'); m['w26'] = r.word('shininess strength')
    blend, two_sided, wire = (r.int('material switch') for _ in range(3))
    if ver < 6:       # MAT6 carries the same bits in its flags word (MAT6 text: flags 2 <-> 1;0;0)
        m['flagword'] = (0x2 if blend else 0) | (0x10 if two_sided else 0) | (0x8 if wire else 0)
    m['w2c'] = r.word('texture value')
    if ver >= 6:
        m['maps'] = [(_text_name(r.string('map')), r.word('map value')) for _ in range(3)]
        m['extra'] = [(_text_name(r.string('map')), r.word('map value')) for _ in range(2)]
    else:
        n = 2 if ver == 3 else 3
        m['maps'] = [(r.word('map id'), r.word('map value')) for _ in range(n)]
        m['maps'] += [(0, 0)] * (3 - n)          # the binary layout (bob1.read_material) reads 3 pairs
    return m


def _read_text_normals(block):
    body = block.strip()[2:] if block.strip().startswith('N:') else None
    if body is None:
        return None
    vals = [v for v in re.split(r'[;{}\s]+', body) if v]
    try:
        vals = [float(v) for v in vals]
    except ValueError:
        raise FormatError(f'text: bad normal block {block[:60]!r}') from None
    if len(vals) == 3:
        return [tuple(vals)] * 3
    if len(vals) == 9:
        return [tuple(vals[0:3]), tuple(vals[3:6]), tuple(vals[6:9])]
    raise FormatError(f'text: normal block with {len(vals)} values')


def _face_normals(verts, raw):
    """Normals for the faces without an N block, as the compiled twins carry them: a face with
    smoothing group 0 gets its own normal cross(b - a, c - a) on every corner; otherwise a corner
    gets the normalised sum of the (area-weighted) normals of the record's faces that use the same
    vertex and share a smoothing bit with it."""
    fn = []
    for f in raw:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = (verts[i] for i in f['abc'])
        ux, uy, uz, vx, vy, vz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
        fn.append((uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx))
    around = {vi: [] for f in raw if f['normals'] is None and f['smooth'] for vi in f['abc']}
    for k, f in enumerate(raw):
        for vi in f['abc']:
            if vi in around:
                around[vi].append(k)

    def unit(v):
        n = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
        return (v[0] / n, v[1] / n, v[2] / n) if n else (0.0, 0.0, 0.0)
    for k, f in enumerate(raw):
        if f['normals'] is not None:
            continue
        if not f['smooth']:
            f['normals'] = [unit(fn[k])] * 3
            continue
        out = []
        for vi in f['abc']:
            sx = sy = sz = 0.0
            for j in around[vi]:
                if j == k or raw[j]['smooth'] & f['smooth']:
                    sx += fn[j][0]; sy += fn[j][1]; sz += fn[j][2]
            out.append(unit((sx, sy, sz)))
        f['normals'] = out


def _read_text_lod(r, value, info):
    verts = []
    while True:
        x, y, z = r.int('vertex x'), r.int('vertex y'), r.int('vertex z')
        if (x, y, z) == (-1, -1, -1):
            break
        verts.append((x, y, z))
    pos = [tuple(int(math.floor(c / TEXT_POS_DIVISOR + 0.5)) for c in v) for v in verts]
    raw, parts = [], []
    while True:
        if r.peek() == '-99' and parts:
            r.next()
            flags = r.bits('body flags')
            break
        part, bounds = {'faces': []}, None
        while True:
            if r.is_magic():
                block = r.magic_field().strip()
                if block.startswith('PART_VALUES_RAW:'):
                    vals = [v for v in re.split(r'[;\s]+', block[16:]) if v]
                    if len(vals) != 10:
                        raise r.error('PART_VALUES_RAW needs 10 values')
                    bounds = [_text_int(v, 'part value') for v in vals]
                elif block.startswith('COLLISION_BOX:'):
                    info['collision_boxes'] += 1       # not mapped: census/overlay refuse text_collision_box
                else:
                    raise r.error(f'unknown /! block {block[:40]!r}')
                continue
            mat = r.int('face material')
            if mat == -99:
                pflags = r.bits('part flags')
                break
            abc = (r.int('face vertex'), r.int('face vertex'), r.int('face vertex'))
            for vi in abc:
                if not 0 <= vi < len(verts):
                    raise r.error(f'face vertex {vi} outside 0..{len(verts) - 1}')
            fflag = r.int('face flags')
            if fflag >= 0 or not -fflag & FACE_BASE or -fflag & ~(FACE_BASE | FACE_UV | FACE_SMOOTH):
                raise r.error(f'unsupported face flags {fflag}')
            bits = -fflag
            smooth = r.int('smoothing group', (I32[0], U32[1])) if bits & FACE_SMOOTH else 0
            uvs = [(r.num('u'), r.num('v')) for _ in range(3)] if bits & FACE_UV else None
            block = r.peek_magic()
            normals = _read_text_normals(r.magic_field()) if block is not None and block.strip().startswith('N:') else None
            f = {'mat': mat, 'abc': abc, 'smooth': smooth & 0xffffffff, 'uvs': uvs, 'normals': normals}
            if normals is None and f['smooth']:
                info['inferred_normals'] += 1           # smoothed normal derived by _face_normals (no twin)
            raw.append(f)
            part['faces'].append(f)
        part['flags'] = pflags
        part['bounds'] = bounds
        parts.append(part)
    if any(f['normals'] is None for f in raw):
        _face_normals(verts, raw)
    # Points of the compiled twins: one point per distinct (vertex, uv, smoothing group) corner,
    # plus the normal when the group is 0 (flat faces), in order of first use over the faces of
    # every part of the record; a smoothed point takes the normal of its first corner.
    points, index, out = [], {}, []
    for part in parts:
        groups, order = {}, []
        for f in part['faces']:
            face = []
            for k, vi in enumerate(f['abc']):
                pflags = 0x19
                vals = list(pos[vi])
                if f['uvs']:
                    pflags |= 2
                    vals += [_fixed(f['uvs'][k][0]), _fixed(f['uvs'][k][1])]
                n = tuple(_fixed(c) for c in f['normals'][k])
                key = (vi, pflags, tuple(vals[3:]), f['smooth'], n if not f['smooth'] else None)
                j = index.get(key)
                if j is None:
                    j = index[key] = len(points)
                    points.append((pflags,) + tuple(vals) + n + (f['smooth'],))
                face.append(j)
            if f['mat'] not in groups:
                groups[f['mat']] = []
                order.append(f['mat'])
            groups[f['mat']].append((face[0], face[1], face[2], 1))
        new = {'flags': part['flags'] & ~PART_PRECOMPUTED,
               'groups': [{'material': m, 'faces': groups[m]} for m in order]}
        if part['bounds'] is not None:
            new['flags'] |= PART_PRECOMPUTED
            new['bounds'] = part['bounds']
            for g in new['groups']:
                g['extra'] = []
        out.append(new)
    return {'value': value, 'flags': flags, 'points': points, 'parts': out}


def parse_text(data):
    """Parse a decoded text body (.bod/.pbd) into the tree parse_binary returns, so serialise()
    writes the compiled BOB1 form. Raises FormatError on anything outside the established
    grammar (body-format-bob1.md section 8); a text scene is refused. The tree also carries
    'text': {'inferred_normals': faces with a smoothing group and no N: block (normal derived,
    rule not validated by a twin), 'collision_boxes': COLLISION_BOX blocks (read, not mapped)};
    serialise() ignores it."""
    if text_kind(data) == 'scene':
        raise FormatError('text scene (VER:/P lines), not a body')
    r = TextReader(data)
    sections, mats, mat_tag = [], [], None
    while not r.done() and r.peek().startswith('MATERIAL'):
        key, _, first = r.next().partition(':')
        key = key.strip().upper()
        if key not in TEXT_MATERIALS:
            raise r.error(f'unsupported material record {key!r}')
        tag = TEXT_MATERIALS[key]
        if mat_tag not in (None, tag):
            raise r.error(f'mixed material records {mat_tag} and {tag}')
        mat_tag = tag
        mats.append(_read_text_material(r, key, first.strip()))
    if r.info is not None:
        sections.append(('INFO', r.info.encode('latin1')))
    if mat_tag:
        sections.append((mat_tag, mats))
    ladder, info = [], {'inferred_normals': 0, 'collision_boxes': 0}
    while not r.done():
        ladder.append(_read_text_lod(r, r.int('body size'), info))
    if not ladder:
        raise FormatError('text body has no body record')
    if len(ladder) > 0xffff:
        raise FormatError('too many LOD records')
    sections.append(('BODY', ladder))
    return {'sections': sections, 'text': info}


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
