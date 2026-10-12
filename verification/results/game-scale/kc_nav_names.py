#!/usr/bin/env python3
"""KC object L/x3story.obj: which navigation-related methods, natives and strings exist (stock AP vs installed).

Decoding as verification/results/gui-scale/kc_gui_calls.py (XOR 0x33 member, STRG chained decode, CLAS
method table, native call '82 <BE32 STRG offset>', 32-bit push '07 <BE32>'). Prints derived names, counts and
the decoded GetFormationDist/GetSize overrides; no script bytes are written.
"""
import bisect, collections, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path('/mnt/c/X3')
KEYS = re.compile(r'Formation|Escort|Follow|Dock|Avoid|Collision|Collide|Distance|Dist\b|Spacing|Wing|Approach|'
                  r'Evade|Obstacle|Gate|Autopilot|Steer|Range|Speed|Radius|Size', re.I)


def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))


def decode(obj):
    code_len = struct.unpack_from('>I', obj, 0x10)[0]; code = obj[0x14:0x14 + code_len]
    s = obj.find(b'STRG'); n = struct.unpack_from('>I', obj, s + 4)[0]; src = obj[s + 8:s + 8 + n]
    strg = bytearray(n); strg[0] = ~src[0] & 255
    for i in range(1, n): strg[i] = (~src[i] - src[i - 1]) & 255
    c = obj.find(b'CLAS'); n = struct.unpack_from('>I', obj, c + 4)[0]; clas = obj[c + 8:c + 8 + n]

    def name(o):
        if 0 < o < len(strg) and strg[o - 1] == 0:
            e = strg.find(b'\0', o); t = bytes(strg[o:e])
            if 1 < len(t) < 80 and all(32 < ch < 127 for ch in t): return t.decode()
    entries = {}
    for i in range(0, len(clas) - 16):
        e, nm, loc, args = struct.unpack_from('>IIII', clas, i)
        if 0 < e < len(code) and loc < 256 and args < 64 and name(nm): entries.setdefault(e, name(nm))
    return code, bytes(strg), entries


def scan(label, obj):
    code, strg, entries = decode(obj)
    keys = sorted(entries)
    ends = {k: (keys[i + 1] if i + 1 < len(keys) else len(code)) for i, k in enumerate(keys)}
    meths = sorted({v for v in entries.values() if KEYS.search(v)})
    print(f'== {label}: methods {len(entries)}, nav-related methods {len(meths)}')
    for m in meths: print('   method', m)
    natives = sorted({t.decode() for t in re.findall(rb'\x00((?:SA|SE|SG|SN|S|INS|X2)_[A-Za-z0-9_]+)', strg)
                      if KEYS.search(t.decode())})
    print(f'   nav-related native/const strings {len(natives)}')
    for t in natives:
        m0 = re.search(rb'\x00' + re.escape(t.encode()) + rb'\x00', strg)
        sites = code.count(b'\x82' + struct.pack('>I', m0.start() + 1)) if m0 else 0
        print(f'   {t:45s} call sites {sites}')
    # GetFormationDist / GetSize overrides, decoded by pattern (opcodes per chase-view-transition.md:
    # 05/06/07 push byte/word/dword, 0f load member, 34 jf, 82 native, 83 return, 87 call virtual by name,
    # 88 call on self; 0x50 = integer multiply, handler 0x004a3387 -> 0x004a9100 imul)
    def sname(o):
        e = strg.find(b'\0', o); return strg[o:e].decode('latin-1')
    for want in ('GetFormationDist', 'GetSize'):
        for e, nm in sorted(entries.items()):
            if nm != want: continue
            seg = code[e:ends[e]]
            desc = []
            for i in range(len(seg) - 4):
                if seg[i] == 0x82:
                    o = struct.unpack('>I', seg[i + 1:i + 5])[0]
                    if 0 < o < len(strg) and strg[o - 1] == 0: desc.append('native ' + sname(o))
                if seg[i] == 0x87:
                    o = struct.unpack('>I', seg[i + 1:i + 5])[0]
                    if 0 < o < len(strg) and strg[o - 1] == 0: desc.append('virtual ' + sname(o))
                if seg[i] == 0x88:
                    o = struct.unpack('>I', seg[i + 1:i + 5])[0]
                    if o in entries: desc.append('self ' + entries[o])
            for m in re.finditer(rb'\x07(....)', seg, re.S):
                v = struct.unpack('>i', m.group(1))[0]
                if 0x8000 <= v < 0x100000: desc.append(f'push {v} (16.16 = {v / 65536:g})')
            for m in re.finditer(rb'\x05(.)\x50', seg, re.S): desc.append(f'x {m.group(1)[0]} (op 0x50 imul)')
            for m in re.finditer(rb'\x06(..)\x83', seg, re.S): desc.append(f'return {struct.unpack(">H", m.group(1))[0]}')
            for m in re.finditer(rb'\x07(....)\x83', seg, re.S): desc.append(f'return {struct.unpack(">i", m.group(1))[0]}')
            print(f'   {nm} @CODE+{e:#x} ({len(seg)} B): ' + '; '.join(dict.fromkeys(desc)))

stock = member(X3 / 'addon/04.cat', 'L/x3story.obj')
latest = None
for cat in sorted((X3 / 'addon').glob('*.cat')):
    b = member(cat, 'L/x3story.obj')
    if b: latest = (cat.relative_to(X3).as_posix(), b)
scan('addon/04.cat L/x3story.obj (stock)', stock)
if latest and latest[1] != stock:
    scan(f'{latest[0]} L/x3story.obj (installed)', latest[1])
