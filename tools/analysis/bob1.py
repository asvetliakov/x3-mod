#!/usr/bin/env python3
"""BOB1 binary body (.pbb/.bob) reader and writer, and the text body (.bod/.pbd) reader.

Layout from the engine parser X3AP.exe 0x00481aa0, tag table 0x0054ed50
(docs/reverse-engineering/body-format-bob1.md). All values are big-endian.
parse() dispatches on the magic like 0x004863c0: parse_binary() returns a plain
tree and serialise() writes it back byte for byte (every opaque field -- material
words, point u32, face word, per-group 7-int records, the 10 part ints -- is kept
as read); parse_text() builds the same tree by the rules of the engine's text
loader 0x00483f20 (body-format-bob1.md section 8, body-text-loader.md), so
serialise() writes a BOB1 body that loads into the model the game builds from
the text. text_kind() tells a text scene (not a body) from a text body.

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
# The engine's own text loader 0x00483f20 (docs/reverse-engineering/body-text-loader.md; body-format-bob1.md
# section 8): parse_text builds the tree the binary parser would have to read to end up with the model
# the engine builds from the same text. Oracle: verification/results/bob1-format/text_loader_reference.py.

SPTYPE_NAMES = {'SPTYPE_LONG': 0, 'SPTYPE_BOOL': 1, 'SPTYPE_FLOAT': 2, 'SPTYPE_FLOAT2': 3, 'SPTYPE_FLOAT3': 4,
                'SPTYPE_FLOAT4': 5, 'SPTYPE_MATRIX3': 6, 'SPTYPE_MATRIX4': 7, 'SPTYPE_STRING': 8}
FACE_UV, FACE_SMOOTH, FACE_UV2 = 0x08, 0x10, 0x80      # face flag = -(bits); other bits only reach the face word
TEXT_MATERIALS = {'MATERIAL5': 'MAT5', 'MATERIAL6': 'MAT6'}
TEXT_LIMITS = dict(materials=100, parts=200, faces=200000, face_vertices=4)   # 0x00483f20 errors beyond these
_TEXT_INT = re.compile(r'-?(?:%[01]+|0[xX][0-9a-fA-F]+|[0-9]+)')              # 0x004eb930: -, %binary, 0x hex, decimal
_TEXT_NUM = re.compile(r'-?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)')                # 0x004ec490: decimal only
I32, U32 = (-0x80000000, 0x7fffffff), (0, 0xffffffff)
_SCENE_LINE = re.compile(rb'^[ \t]*(VER[ \t]*:|P[ \t]+-?\d+[ \t]*;)', re.M)


def text_kind(data):
    """'scene' for a text scene (VER:/P n; node lines, the text twin of CUT1), else 'body'."""
    return 'scene' if _SCENE_LINE.search(bytes(data[:65536])) else 'body'


def _int_value(v):
    neg = v.startswith('-')
    t = v[1:] if neg else v
    n = int(t[1:], 2) if t.startswith('%') else int(t, 16) if t[:2].lower() == '0x' else int(t)
    return -n if neg else n


class TextReader:
    """Field stream of a text body as the engine lexes it (0x004e98a0): ';'-separated values, and
    '/' starts a comment to the end of the line, so every /! ... !/ block (N:, PART_VALUES_RAW,
    COLLISION_BOX) and anything after it on its line is ignored. A /! without !/ on its line is
    refused (the engine would read the block's later lines as data). fields = [(value, line)]."""

    def __init__(self, data):
        data = bytes(data)
        text = (data[3:] if data.startswith(b'\xef\xbb\xbf') else data).decode('latin1')
        self.info = None
        lines = text.split('\n')
        for n, line in enumerate(lines):
            cut = line.find('/')
            if cut < 0:
                continue
            rest = line[cut:]
            if rest.startswith('/!') and '!/' not in rest[2:]:
                raise FormatError(f'text line {n + 1}: unclosed /! block')
            if self.info is None and rest.startswith('/#'):
                self.info = rest[2:].strip()
            lines[n] = line[:cut]
        self.fields = self._split(lines)
        self.i = 0

    @staticmethod
    def _split(lines):
        out, pending = [], ''
        for n, chunk in enumerate(lines, 1):
            parts = chunk.split(';')
            parts[0] = pending + '\n' + parts[0] if pending.strip() else parts[0]
            for p in parts[:-1]:
                out.append((p.strip(), n))
            pending = parts[-1]
        if pending.strip():
            raise FormatError(f'text: unterminated value {pending.strip()[:40]!r} at line {len(lines)}')
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

    def int(self, what='integer', span=I32):
        v = self.next()
        if not _TEXT_INT.fullmatch(v):
            self.i -= 1
            raise self.error(f'expected {what}, got {v[:40]!r}')
        n = _int_value(v)
        if not span[0] <= n <= span[1]:
            self.i -= 1
            raise self.error(f'{what} {v[:40]} outside {span[0]}..{span[1]}')
        return n

    def fixed(self, what='number'):
        """16.16 value as 0x004ec490 + _ftol read it: decimal, times 65536.0, truncated."""
        v = self.next()
        n = int(float(v) * 65536.0) if _TEXT_NUM.fullmatch(v) else None
        if n is None or not I32[0] <= n <= I32[1]:
            self.i -= 1
            raise self.error(f'expected a 16.16 {what}, got {v[:40]!r}')
        return n

    def string(self, what='string'):
        v = self.next()
        if not v:
            self.i -= 1
            raise self.error(f'expected {what}, got an empty value')
        return v.encode('latin1')

    def bits(self, what):
        """A flags value written as a binary digit string (0000000001000000 = 0x40), 32 bits at most."""
        v = self.next()
        if not v or set(v) - {'0', '1'} or int(v, 2) > 0xffffffff:
            self.i -= 1
            raise self.error(f'{what} {v[:40]!r} is not a binary digit string of at most 32 bits')
        return int(v, 2)

    def word(self, what='16-bit value'):
        return self.int(what, (0, 0xffff))


def _text_name(v):
    return b'' if v.upper() == b'NULL' else v


def _read_text_material(r, key, first):
    """One MATERIAL5/MATERIAL6 record as 0x0048447c..0x00484a30 reads it; `first` is the value after
    'MATERIALn:' (the index). The 12 colour words are rgb x3, then +0x16 = -1 (MAT5/6), the
    transparency word (+0x18) and self-illumination (+0x1a); the blend/two-sided/wire switches
    are OR-ed into the flags as 0x2/0x10/0x8 (MAT5 flag word; MAT6 flags, which the engine then
    overwrites with the texture's table flags when the texture resolves, before the OR)."""
    if key not in TEXT_MATERIALS:
        raise r.error(f'unsupported material record {key!r}' + (
            ' (MATERIAL3: the engine maps each record to the nearest global material, 0x004f71f0,'
            ' which needs the running game)' if key == 'MATERIAL3' else ''))
    ver = MATVER[TEXT_MATERIALS[key]]
    if not _TEXT_INT.fullmatch(first) or not 0 <= _int_value(first) <= 0xffff:
        raise r.error(f'bad material index {first[:40]!r}')
    m = {'index': _int_value(first)}
    if ver >= 6:
        m['flags'] = r.int('material flags', U32)
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
                    val = [r.fixed('parameter value') for _ in range(SPTYPE_WORDS[typ])]
                params.append((name, typ, val))
            m['params'] = params
            return m
        tex = r.string('texture')
        if _TEXT_INT.fullmatch(tex.decode('latin1')):
            raise r.error('MATERIAL6 with a numeric texture (MATERIAL3 layout) is not supported')
        m['texture'] = _text_name(tex)
    else:
        m['texture'] = r.word('texture id')
    rgb = [r.word('colour') for _ in range(9)]
    m['colors'] = rgb + [0xffff, r.word('transparency'), r.word('self illumination')]
    m['w24'] = r.word('shininess'); m['w26'] = r.word('shininess strength')
    switches = sum(bit for bit in (0x2, 0x10, 0x8) if r.int('material switch'))
    if ver >= 6:
        m['flags'] |= switches
    else:
        m['flagword'] = switches
    m['w2c'] = r.word('texture value')
    if ver >= 6:
        m['maps'] = [(_text_name(r.string('map')), r.word('map value')) for _ in range(3)]
        m['extra'] = [(_text_name(r.string('map')), r.word('map value')) for _ in range(2)]
    else:
        m['maps'] = [(r.word('map id'), r.word('map value')) for _ in range(3)]
    return m


# fixed-point helpers of 0x00480830 / 0x00469c20 / 0x00469a50 (body-text-loader.md section 5)

def _i32(v):
    v &= 0xffffffff
    return v - 0x100000000 if v & 0x80000000 else v


def _fm(a, b):                      # imul; add 0x8000; adc; shrd 16 (low 32 bits)
    return _i32((a * b + 0x8000) >> 16)


def _cdiv(a, b):                    # idiv: truncation toward zero
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def _sqrtfix(v):                    # 0x00469a50, bit for bit
    v &= 0xffffffff
    root, bit, rem = 0, 0x40000000, v
    while bit:
        if rem >= bit and rem - bit >= root:
            rem -= bit + root
            root = (root >> 1) | bit
        else:
            root >>= 1
        bit >>= 2
    bit, root, rem = 0x4000, (root << 16) & 0xffffffff, (rem << 16) & 0xffffffff
    while bit:
        if rem >= bit and rem - bit >= root:
            rem -= bit + root
            root = (root >> 1) | bit
        else:
            root >>= 1
        bit >>= 2
    return root


def _normalise(x, y, z):            # 0x00469c20
    if x == 0 and y == 0 and z == 0:
        return 0, 0, 0
    while not (abs(x) < 0x600000 and abs(y) < 0x600000 and abs(z) < 0x600000):
        x, y, z = _cdiv(x, 4), _cdiv(y, 4), _cdiv(z, 4)
    while abs(x) <= 0x17ffff and abs(y) <= 0x17ffff and abs(z) <= 0x17ffff:
        x, y, z = x * 4, y * 4, z * 4
    n = _sqrtfix(_fm(x, x) + _fm(y, y) + _fm(z, z))
    if n == 0:
        return 0, 0, 0
    return _i32(_cdiv(x << 16, n)), _i32(_cdiv(y << 16, n)), _i32(_cdiv(z << 16, n))


_ASIN = None


def _asin_table():                  # 0x004f0110: T[i] = trunc(asin(i/65536) / (4 asin 1) * 65536)
    global _ASIN
    if _ASIN is None:
        quarter = 4 * math.asin(1.0)
        _ASIN = [int(math.asin(i / 65536.0) / quarter * 65536.0) for i in range(0x10001)]
    return _ASIN


_F32 = struct.Struct('<f')


def _corner_weight(p, q, r, table):  # 0x00480d65..0x00480f60: angle at p in 1/65536 turns
    d1 = (p[0] - q[0], p[1] - q[1], p[2] - q[2])
    d2 = (p[0] - r[0], p[1] - r[1], p[2] - r[2])
    l1 = int(math.sqrt(_F32.unpack(_F32.pack(float(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2])))[0]))
    l2 = int(math.sqrt(_F32.unpack(_F32.pack(float(d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2])))[0]))
    dot = _i32(_fm(d1[0], d2[0]) + _fm(d1[1], d2[1]) + _fm(d1[2], d2[2]))
    den = _fm(l1, l2)
    c = 0 if den == 0 else _i32(_cdiv(dot << 16, den))
    if c < 0:
        return (table[-c] if -c <= 0x10000 else 0x4000) + 0x4000
    return 0x4000 - table[c] if c <= 0x10000 else 0


def _read_text_lod(r, value):
    """One record (0x00483f20 record loop, 0x00480830 per face, section 4-5 of the loader note)."""
    verts = []
    while True:
        x, y, z = r.int('vertex x'), r.int('vertex y'), r.int('vertex z')
        if (x, y, z) == (-1, -1, -1):
            break
        verts.append((x, y, z))
    if r.peek() is not None and r.peek().upper().startswith('WEIGHTS'):
        raise r.error('WEIGHTS: records are not supported')
    m = max((max(abs(c) for c in v) for v in verts), default=0)
    if m == 0:
        raise r.error('record without a non-zero vertex (the engine returns no model)')
    norm = [(_cdiv(v[0] << 16, m), _cdiv(v[1] << 16, m), _cdiv(v[2] << 16, m)) for v in verts]
    table = _asin_table()
    pts, pos, uvs, smooths, normals = [], [], [], [], []      # point attributes, parallel lists
    shared = {}                                               # (s, position) -> [point]
    parts, uv2_seen, negative = [], False, False
    first = r.int('face material')
    while first != -99:
        if len(parts) == TEXT_LIMITS['parts']:
            raise r.error(f'more than {TEXT_LIMITS["parts"]} parts in a record')
        faces, mat = [], first
        while mat != -99:
            idx = []
            x = r.int('face vertex')
            while x >= 0:
                if x >= len(verts):
                    raise r.error(f'face vertex {x} outside 0..{len(verts) - 1}')
                idx.append(x)
                x = r.int('face vertex')
            if len(idx) > TEXT_LIMITS['face_vertices']:
                raise r.error(f'face with {len(idx)} vertices (the engine takes at most 4)')
            bits = -x
            smooth, uv, uv2 = 0, [(0, 0)] * len(idx), None
            if len(idx) > 2:
                if bits & FACE_SMOOTH:
                    smooth = r.int('smoothing group', (I32[0], U32[1])) & 0xffffffff
                if bits & FACE_UV:
                    uv = [(r.fixed('u'), r.fixed('v')) for _ in idx]
                if bits & FACE_UV2:
                    uv2 = [(r.fixed('u2'), r.fixed('v2')) for _ in idx]
                    uv2_seen = True
            faces.append((mat, idx, uv, uv2 or uv, bits & ~1, smooth))
            negative |= mat < 0
            if len(faces) >= TEXT_LIMITS['faces']:
                raise r.error(f'{TEXT_LIMITS["faces"]} faces or more in a part')
            mat = r.int('face material')
        pflags = r.bits('part flags')
        if pflags & 4:
            raise r.error('part flag 4 (pivot vertex) cannot be written to BOB1')
        groups, order = {}, []
        for mat, idx, uv, uv2, word, smooth in faces:
            corner = []
            for k, vi in enumerate(idx):
                u, v = uv[k]
                if pflags & 2:
                    u, v = u & 0xffff, v & 0xffff
                u2, v2 = uv2[k]
                p = norm[vi]
                j = None
                if smooth:
                    for c in shared.get((smooth, p), ()):
                        pu, pv, pu2, pv2 = uvs[c]
                        if abs(pu - u) < 2 and abs(pv - v) < 2 and abs(pu2 - u2) < 2 and abs(pv2 - v2) < 2:
                            j = c
                            break
                if j is None:
                    j = len(pos)
                    pos.append(p); uvs.append((u, v, u2, v2)); smooths.append(smooth); normals.append([0, 0, 0])
                    if smooth:
                        shared.setdefault((smooth, p), []).append(j)
                corner.append(j)
            if mat not in groups:
                groups[mat] = []
                order.append(mat)
            for t in range(2, len(corner)):
                tri = (corner[0], corner[t - 1], corner[t])
                groups[mat].append(tri + (word,))
                a, b, c = pos[tri[0]], pos[tri[1]], pos[tri[2]]
                bx, by, bz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
                cx, cy, cz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
                n = _normalise(_fm(by, cz) - _fm(bz, cy), _fm(bz, cx) - _fm(bx, cz), _fm(bx, cy) - _fm(by, cx))
                if smooth == 0:
                    for i in tri:
                        normals[i] = list(n)
                else:
                    for p_, q_, r_ in ((tri[0], tri[1], tri[2]), (tri[1], tri[2], tri[0]), (tri[2], tri[0], tri[1])):
                        w = _corner_weight(pos[p_], pos[q_], pos[r_], table)
                        acc = normals[p_]
                        normals[p_] = [_i32(acc[k] + _fm(n[k], w)) for k in range(3)]
        parts.append({'flags': pflags, 'groups': [{'material': m_, 'faces': groups[m_]} for m_ in order]})
        first = r.int('face material')
    lodflags = r.bits('body flags') | (1 if negative else 0) | (0x20 if uv2_seen else 0)
    pflag = 0x1f if uv2_seen else 0x1b
    points = []
    for p, (u, v, u2, v2), s, n in zip(pos, uvs, smooths, normals):
        n = _normalise(*n)
        points.append((pflag,) + p + ((u, v, u2, v2) if uv2_seen else (u, v)) + n + (s,))
    return {'value': value, 'flags': lodflags, 'points': points, 'parts': parts}


def parse_text(data):
    """Parse a decoded text body (.bod/.pbd) the way the engine's text loader 0x00483f20 does and
    return the tree parse_binary returns, so serialise() writes a BOB1 body that loads into the
    same model (body-format-bob1.md section 8, body-text-loader.md): /! blocks ignored, each
    record normalised to max |coordinate| 65536 with truncation, 16.16 values truncated, points
    and normals built by the engine rule, face word = flags & ~1, no tangent records, effect
    parameters as written. Raises FormatError on anything outside that grammar, on MATERIAL3
    and MATERIAL (global-material bodies) and on a text scene."""
    if text_kind(data) == 'scene':
        raise FormatError('text scene (VER:/P lines), not a body')
    r = TextReader(data)
    sections, mats, mat_tag = [], [], None
    while not r.done() and r.peek().upper().startswith('MATERIAL'):
        key, _, first = r.next().partition(':')
        key = key.strip().upper()
        tag = TEXT_MATERIALS.get(key)
        if mat_tag not in (None, tag):
            raise r.error(f'mixed material records {mat_tag} and {key}')
        mat = _read_text_material(r, key, first.strip())
        mat_tag = tag
        mats.append(mat)
        if len(mats) > TEXT_LIMITS['materials']:
            raise r.error(f'more than {TEXT_LIMITS["materials"]} materials')
    if r.info is not None:
        sections.append(('INFO', r.info.encode('latin1')))
    if mat_tag:
        sections.append((mat_tag, mats))
    ladder = []
    while not r.done():
        ladder.append(_read_text_lod(r, r.int('body size')))
    if not ladder:
        raise FormatError('text body has no body record')
    if len(ladder) > 0xffff:
        raise FormatError('too many LOD records')
    sections.append(('BODY', ladder))
    return {'sections': sections}


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
