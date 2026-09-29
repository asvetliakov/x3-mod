#!/usr/bin/env python3
"""Static scan of the KC script objects for the GUI-scale question (docs/reverse-engineering/gui-scale.md).

Reads L/x3story.obj from addon/04.cat (stock AP) and from the newest addon catalogue that carries it
(the installed override) in memory (XOR 0x33, STRG decoding and the native-call encoding
`82 <BE32 STRG name>` as in verification/results/field-of-view/kc_fov_calls.py). Prints only derived
names, CODE offsets and counts; no script bytes are written.
"""
import bisect, collections, hashlib, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
NATIVES = ('B3D_ScreenGetWidth', 'B3D_ScreenGetHeight', 'B3D_ScreenGetAspectX', 'B3D_ScreenGetAspectY',
           'B3D_ScreenGetBorderX', 'B3D_ScreenGetBorderY', 'B3D_OpenFont', 'B3D_InstSetFlags2',
           'B3D_InstGetFlags2', 'B3D_CameraSetViewPort', 'B3D_SceneSetViewPort', 'B3D_CameraSetAspect',
           'B3D_CameraSetAspectRatio', 'B3D_SceneSetSystemScale', 'B3D_TexTextBlock', 'B3D_InstSetScale',
           'B3D_InstSetSize', 'X2_UpdateCursorSteering',
           'INS_CockpitGetObjectByTargetOverlayIconPos', 'INS_CockpitGetCursorAim',
           'INS_CockpitGetMenuPosByTargetOverlayIconPos', 'INS_CockpitProjectPosition')


def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))


def scan(label, obj, detail):
    print(f'== {label}: sha256 {hashlib.sha256(obj).hexdigest()}')
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
    for i in range(0, len(clas) - 16):
        e, nm, loc, args = struct.unpack_from('>IIII', clas, i)
        if 0 < e < len(code) and loc < 256 and args < 64 and name(nm): entries.setdefault(e, name(nm))
    keys = sorted(entries)

    def owner(at):
        j = bisect.bisect_right(keys, at) - 1; return entries[keys[j]] if j >= 0 else '?'
    for native in NATIVES:
        m0 = re.search(rb'\x00' + re.escape(native.encode()) + rb'\x00', strg)
        if not m0:
            print(f'{native:26s} not referenced'); continue
        pat = b'\x82' + struct.pack('>I', m0.start() + 1)
        sites = [m.start() for m in re.finditer(re.escape(pat), code)]
        methods = collections.Counter(owner(a) for a in sites)
        print(f'{native:26s} {len(sites):4d} sites in {len(methods)} methods')
        if native in detail:
            for meth, k in sorted(methods.items(), key=lambda kv: -kv[1])[:detail[native]]:
                print(f'    {meth} x{k}')
    # 32-bit constants pushed within 24 bytes before each InstSetFlags2 call (07 <BE32> push form)
    m0 = re.search(rb'\x00B3D_InstSetFlags2\x00', strg)
    pat = b'\x82' + struct.pack('>I', m0.start() + 1)
    consts = collections.Counter()
    for m in re.finditer(re.escape(pat), code):
        seg = code[max(0, m.start() - 24):m.start()]
        for k in re.finditer(rb'\x07(....)', seg, re.S):
            v = struct.unpack('>I', k.group(1))[0]
            if v & 0x200 and v < 0x1000000: consts[hex(v)] += 1
    print('InstSetFlags2 nearby 32-bit pushes with 0x200:', dict(consts.most_common(12)))


stock = member(X3 / 'addon/04.cat', 'L/x3story.obj')
latest = None
for cat in sorted((X3 / 'addon').glob('*.cat')):
    b = member(cat, 'L/x3story.obj')
    if b: latest = (cat.relative_to(X3), b)
detail = {'INS_CockpitGetObjectByTargetOverlayIconPos': 5, 'INS_CockpitGetCursorAim': 5,
          'B3D_ScreenGetWidth': 40, 'B3D_ScreenGetHeight': 40, 'B3D_OpenFont': 10,
          'B3D_ScreenGetAspectX': 10, 'B3D_ScreenGetAspectY': 10, 'B3D_SceneSetSystemScale': 10}
scan('addon/04.cat (stock)', stock, detail)
if latest and latest[1] != stock:
    scan(f'{latest[0]} (installed override)', latest[1],
         {'INS_CockpitGetObjectByTargetOverlayIconPos': 5, 'INS_CockpitGetCursorAim': 5})
intro = member(X3 / 'addon/04.cat', 'L/x3intro.obj')
scan('addon/04.cat L/x3intro.obj (stock)', intro,
     {'B3D_ScreenGetWidth': 20, 'B3D_ScreenGetHeight': 20, 'B3D_InstSetFlags2': 20,
      'B3D_ScreenGetAspectX': 10, 'B3D_OpenFont': 5, 'B3D_InstSetSize': 5})
