#!/usr/bin/env python3
"""Static checks behind docs/reverse-engineering/gui-scale.md.

Reads X3AP.exe and the catalogues read-only; writes only derived facts
(addresses, constants, counts, names) to gui_scale_static_checks.json next to
this script. No game bytes are copied.

  python3 verification/results/gui-scale/gui_scale_static_checks.py [game_root]
"""
import gzip, hashlib, json, re, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
from inspect_x3 import read_catalogue  # noqa: E402

GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else \
    Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
exe = (GAME / 'X3AP.exe').read_bytes()

pe = struct.unpack_from('<I', exe, 0x3c)[0]
nsec = struct.unpack_from('<H', exe, pe + 6)[0]
optsz = struct.unpack_from('<H', exe, pe + 20)[0]
base = struct.unpack_from('<I', exe, pe + 24 + 28)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', exe, o + 8)
    secs.append((va + base, ro, rs))


def off(va):
    for sva, ro, rs in secs:
        if sva <= va < sva + rs:
            return va - sva + ro
    raise ValueError(hex(va))


def va_of(o):
    for sva, ro, rs in secs:
        if ro <= o < ro + rs:
            return sva + o - ro
    return None


def u32(va):
    return struct.unpack_from('<I', exe, off(va))[0]


def f32(va):
    return struct.unpack_from('<f', exe, off(va))[0]


def cstr(va):
    o = off(va)
    return exe[o:exe.index(b'\0', o)].decode('latin1')


def find_str(s):
    m = re.search(re.escape(b'\0' + s + b'\0'), exe)
    return hex(va_of(m.start() + 1)) if m else None


def call_target(va):
    o = off(va)
    if exe[o] != 0xE8:
        return None
    return hex((va + 5 + struct.unpack_from('<i', exe, o + 1)[0]) & 0xffffffff)


out = {'exe_sha256': hashlib.sha256(exe).hexdigest()}
out['strings'] = {s.decode(): find_str(s) for s in
                  (b'-fontscale', b'MPF_FONTSCALE', b'%s%d', b'%s_l', b'true\\%d')}
# Command-line switch gate at 0x004ec9e0: ((cfg[+0xd8]+4) & 1) == 0 || (... & 2) != 0
static = 0x57c008
flags = u32(static + 4)
out['switch_gate'] = {'static_struct': hex(static), 'name': cstr(u32(static)),
                      'flags_plus4': hex(flags),
                      'fontscale_switch_parsed': (flags & 1) == 0 or (flags & 2) != 0,
                      'font_path_format': cstr(u32(static + 0x3c))}
out['ortho_constants'] = {'0x005655d0': f32(0x5655d0), '0x005655a4': f32(0x5655a4),
                          '0x005654e0': f32(0x5654e0)}
table = 0x57a420
names = []
i = 0
while True:
    p = u32(table + 4 * i)
    if not 0x532000 <= p < 0x57e000:
        break
    names.append(cstr(p))
    i += 1
want = ('B3D_ScreenGetWidth', 'B3D_ScreenGetHeight', 'B3D_ScreenGetAspectX',
        'B3D_ScreenGetAspectY', 'B3D_ScreenGetBorderX', 'B3D_ScreenGetBorderY',
        'B3D_OpenFont', 'B3D_InstSetFlags2', 'B3D_InstSetPos', 'B3D_InstSetSize',
        'B3D_CameraSetViewPort', 'B3D_CameraSetAspect', 'B3D_SceneSetSystemScale',
        'B3D_TexTextBlock', 'B3D_SetScreenOverlay')
out['b3d_table'] = {'start': hex(table), 'count': len(names), 'dispatcher': hex(0x493b40),
                    'case_index': {n: hex(names.index(n)) for n in want}}
out['sites'] = {
    '0x0047e002': {'call_target': call_target(0x47e002)},
    '0x0047e70c': {'call_target': call_target(0x47e70c)},
    '0x004bdf27': exe[off(0x4bdf27):off(0x4bdf27) + 10].hex(),   # TEST [EBX+0x130],0x200
    '0x004ecae3': exe[off(0x4ecae3):off(0x4ecae3) + 6].hex(),    # MOV [EAX+0x784],ECX (ECX=1)
    '0x004ecff8': exe[off(0x4ecff8):off(0x4ecff8) + 6].hex(),    # MOV [ECX+0x784],EAX (atol)
    # Click selection (gui-scale.md section 6): the two script-point loads in the INS dispatcher 0x0042d340.
    '0x0042ddf1': exe[off(0x42ddf1):off(0x42ddf1) + 8].hex(),    # case 0x28: mov esi,[ebx+6]; mov edi,[ebx+0xb]; jl
    '0x0042ece0': exe[off(0x42ece0):off(0x42ece0) + 8].hex(),    # case 0x64: mov esi,[esi+6]; push ecx; push esi; add eax,..
    '0x0042eceb': {'call_target': call_target(0x42eceb)},
    '0x0042de11': {'call_target': call_target(0x42de11)},
    '0x00425474': {'call_target': call_target(0x425474)},
    '0x00445ad0': {'call_target': call_target(0x445ad0)},
}


def member(cats, path):
    hit = None
    for cat in cats:
        for e in read_catalogue(cat):
            if e['path'] == path:
                hit = (cat, e)
    if hit is None:
        return None
    cat, e = hit
    with open(cat.with_suffix('.dat'), 'rb') as fh:
        fh.seek(e['offset'])
        return str(cat.relative_to(GAME)), fh.read(e['size'])


cats = sorted(GAME.glob('*.cat')) + sorted((GAME / 'addon').glob('*.cat'))


def unpck(b):
    k = b[0] ^ 0x1f
    return gzip.decompress(bytes(x ^ k for x in b)).decode('latin1')


src, raw = member(cats, 'addon/types/Fonts.pck')
out['fonts_table'] = {'source': src, 'rows': [l.strip() for l in unpck(raw).splitlines()
                                               if l.strip() and not l.startswith('#')]}
out['font_files'] = sorted({e['path'] for cat in cats for e in read_catalogue(cat)
                            if e['path'].lower().startswith('f/')})
src, raw = member(cats, 'addon/types/Materials.pck')
rows = [l for l in unpck(raw).splitlines() if l and not l.startswith('/')][1:]
fs_ids = []
for r in rows:
    f = [x.strip() for x in r.split(';')]
    if len(f) > 15 and 'MPF_FONTSCALE' in f[15]:
        fs_ids.append((int(f[12]), f[15]))
true_paths = {Path(e['path']).stem for cat in cats for e in read_catalogue(cat)
              if re.match(r'(?i)tex/true/', e['path'])}
out['materials'] = {'source': src, 'rows': len(rows),
                    'fontscale_ids': [i for i, _ in fs_ids],
                    'fontscale_generated_ids': [i for i, fl in fs_ids if 'MPF_GENERATED' in fl],
                    'fontscale_file_ids': [i for i, fl in fs_ids if 'MPF_GENERATED' not in fl],
                    'hires_variants_present': sorted(str(i + 10000) for i, fl in fs_ids
                                                     if str(i + 10000) in true_paths)}
dst = Path(__file__).with_name('gui_scale_static_checks.json')
dst.write_text(json.dumps(out, indent=1) + '\n')
print(json.dumps(out, indent=1))
