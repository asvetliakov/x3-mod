#!/usr/bin/env python3
"""Static scan of the installed KC script object and Globals.pck for the FOV ("focus") path.

Reads addon/04.cat:L/x3story.obj (XOR 0x33) and every types/Globals.pck in memory; prints only
derived names, CODE offsets and setting values (no script bytes are written anywhere).
Encoding used (docs/reverse-engineering/selection-native-vm.md, chase-view-transition.md):
native call = <push argc> 82 <BE32 STRG name>; 85 <BE32 name> call by name; 86 <BE32 CODE entry> call on class
(class id pushed before as 06 <BE16>); 88 <BE32 CODE entry> call on self; 05/06/07 push 8/16/32-bit.
"""
import bisect, gzip, hashlib, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'

def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))

obj = member(X3 / 'addon/04.cat', 'L/x3story.obj')
print('x3story.obj sha256', hashlib.sha256(obj).hexdigest())
code_len = struct.unpack_from('>I', obj, 0x10)[0]
code = obj[0x14:0x14 + code_len]
s = obj.find(b'STRG'); n = struct.unpack_from('>I', obj, s + 4)[0]; src = obj[s + 8:s + 8 + n]
strg = bytearray(n); strg[0] = ~src[0] & 255
for i in range(1, n): strg[i] = (~src[i] - src[i - 1]) & 255
c = obj.find(b'CLAS'); n = struct.unpack_from('>I', obj, c + 4)[0]; clas = obj[c + 8:c + 8 + n]
def name(o):
    if 0 < o < len(strg) and strg[o - 1] == 0:
        e = strg.find(b'\0', o); t = bytes(strg[o:e])
        if 1 < len(t) < 60 and all(32 < ch < 127 for ch in t): return t.decode()
entries = {}
for i in range(0, len(clas) - 16, 1):
    e, nm, loc, args = struct.unpack_from('>IIII', clas, i)
    if 0 < e < len(code) and loc < 256 and args < 64 and name(nm): entries.setdefault(e, name(nm))
keys = sorted(entries)
def owner(at):
    j = bisect.bisect_right(keys, at) - 1; return f'{entries[keys[j]]}@{keys[j]:#x}'
def offset(text):
    m = re.search(rb'\x00' + re.escape(text) + rb'\x00', strg); return m.start() + 1
for native in ('INS_SetFocus', 'B3D_CameraSetFocus', 'B3D_CameraGetFocus', 'B3D_CameraSetAspectRatio', 'B3D_CameraCalcFOV', 'INS_CockpitSetZooming'):
    pat = b'\x82' + struct.pack('>I', offset(native.encode()))
    for m in re.finditer(re.escape(pat), code):
        print(f'{native:26s} call at CODE {m.start():#08x} in {owner(m.start())}')
set_focus = [e for e, v in entries.items() if v == 'SetFocus'][0]
def decode_args(at):
    # The two call shapes before `06 <class> 86 <entry>`: SA_GetGlobalParameter(<id>, <default>) or a member load.
    seg = code[at - 16:at]
    m = re.search(rb'\x05(.)\x05(.)\x03\x82(....)\x02\x06..$', seg, re.S)
    if m:
        return f'{name(struct.unpack(">I", m.group(3))[0])}(id={m.group(2)[0]:#x}, default={m.group(1)[0]})'
    m = re.search(rb'\x0f(..)\x02\x06..$', seg, re.S)
    return f'member {struct.unpack(">H", m.group(1))[0]:#x}' if m else 'undecoded'
for m in re.finditer(re.escape(b'\x86' + struct.pack('>I', set_focus)), code):
    klass = struct.unpack_from('>H', code, m.start() - 2)[0]  # 06 <BE16 class> pushed just before
    print(f'SetFocus(class {klass:#x}) called at CODE {m.start():#08x} in {owner(m.start())}'
          f' args: {decode_args(m.start())}')
for root in (X3, X3 / 'addon'):
    for cat in sorted(root.glob('*.cat')):
        for e in read_catalogue(cat):
            if e['path'].lower().endswith('types/globals.pck'):
                txt = gzip.decompress(member(cat, e['path'])).decode('latin1')
                print(cat.relative_to(X3), e['path'], [l.strip() for l in txt.splitlines() if 'FOV' in l])
