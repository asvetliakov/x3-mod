#!/usr/bin/env python3
"""Fog-band depth prepass (camera +0x270 & 0x40000) evidence for docs/reverse-engineering/distance-fade.md section 8.

Read-only, no game, no Wine.
1. Checks the X3AP.exe site bytes and strings quoted by the note.
2. Decodes the KC call sites of B3D_CameraSetFlags in the shipped l/x3story.obj, l/x3galedit.obj and
   l/x3intro.obj (STRG decode as in lod-child-hide/kc_instsetflags.py; a native call is
   `<argc+1> 82 <be32 STRG offset>`; `07 <be32>`, `06 <be16>`, `05 <byte>` push, `54` OR, `53` AND;
   opcode meanings inferred from the pattern). Prints the method (CLAS record) and the masks pushed
   between the preceding B3D_CameraGetFlags and the SetFlags call, and the next INS_CockpitSet*Camera
   native after it.
3. Optional: tallies `object_fade ... flags270=` rows in the session logs given on the command line.
Usage: python3 verification/results/distance-fade-prepass/prepass_decode.py [session.log ...]
"""
import bisect, collections, hashlib, os, re, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import sector_fog_census as s, bob1

EXE = os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
SITES = {  # VA: expected bytes
    0x00472280: 'f78670020000000004008bfe74148b4e1c6a045551e886c600008b561c535552eb068b461c535550e873c60000',
    0x0047e8d0: 'f787700200000000040074096a045356e83b0000006a005356e832000000',  # env-map path, same double walk
    0x0047ea18: '565753e8d0e4feff85c00f8544010000',   # instanced-body path 0x0046cef0 skips both walks
    0x0047eab4: 'f644241c040f85a9000000',             # D + r < N: prepass (flag 4) skips the node
    0x0047eb13: 'f644241c04578bce74268b1518856000818e300100000000020083c24c5253e889eeffff81a630010000fffffdffeb25a118',
    0x00494f10: '8b4e06898870020000',                 # B3D_CameraSetFlags: [cam+0x270] = message+6
    0x004c0817: 'f78030010000000002007443681034560033c0e8c1a8ffff83c40485c00f84',  # 0x20000 -> effect "z_only"
    0x004c0c36: '39b7a401000074138b450cf78030010000000002000f8591340000',          # blended subset leaves the prepass
    0x004c21a4: '39b798010000ddd80f849c0d0000',       # z_only g_mWorldViewProjection handle required
    0x00471f7c: '33db',                               # frame routine: EBX = 0 (second walk flag)
}
STRINGS = {0x563410: 'z_only', 0x563418: 'Z_Only_Alpha', 0x563428: 'Z_Only_Fast', 0x563480: 'g_mWorldViewProjection'}

def exe_checks():
    data = open(EXE, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    nsec = struct.unpack_from('<H', data, pe + 6)[0]; opt = struct.unpack_from('<H', data, pe + 20)[0]
    secs = [struct.unpack_from('<8sIIII', data, pe + 24 + opt + 40 * i) for i in range(nsec)]
    def read(va, n):
        rva = va - 0x400000
        for _, vs, sva, rs, ro in secs:
            if sva <= rva < sva + max(vs, rs): return data[ro + rva - sva: ro + rva - sva + n]
    def cstr(va): b = read(va, 64); return b[:b.index(0)].decode('latin1')
    print('exe sha256', hashlib.sha256(data).hexdigest()[:16])
    ok = sum(read(va, len(h) // 2).hex() == h for va, h in SITES.items())
    print('site bytes', f'{ok}/{len(SITES)}')
    ok = sum(cstr(va) == t for va, t in STRINGS.items())
    print('strings', f'{ok}/{len(STRINGS)}')
    t = struct.unpack_from('<I', read(0x0057a420 + 4 * 0x3b, 4))[0]
    print('B3D native 0x3b', cstr(t))

def kc_load(a, fn):
    d = a.read_entry(a.candidates(fn)[-1])
    i = d.find(b'STRG'); src = d[i + 8:i + 8 + struct.unpack_from('>I', d, i + 4)[0]]
    strg = bytearray(len(src)); strg[0] = ~src[0] & 255
    for k in range(1, len(src)): strg[k] = (~src[k] - src[k - 1]) & 255
    ci = d.find(b'CODE'); code = d[ci + 8:ci + 8 + struct.unpack_from('>I', d, ci + 4)[0]]
    j = d.find(b'CLAS'); clas = d[j + 8:j + 8 + struct.unpack_from('>I', d, j + 4)[0]]
    return bytes(strg), code, clas

def kc_sites(a, fn):
    strg, code, clas = kc_load(a, fn)
    name = lambda o: strg[o:strg.find(b'\0', o)].decode('latin1')
    off = lambda n: strg.find(b'\0' + n + b'\0') + 1
    meth = sorted({(c, name(nm)) for k in range(0, len(clas) - 15, 4)
                   for a_, c, nm, l in [struct.unpack_from('>4I', clas, k)]
                   if 0 < c < len(code) and 0 < nm < len(strg) and strg[nm - 1] == 0 and l < 256 and a_ < 64})
    starts = [m[0] for m in meth]
    call = lambda o: b'\x82' + o.to_bytes(4, 'big')
    setf, getf = off(b'B3D_CameraSetFlags'), off(b'B3D_CameraGetFlags')
    cockpit = [(off(n), n.decode()) for n in (b'INS_CockpitSetSectorCamera', b'INS_CockpitSetGalaxyCamera',
                                             b'INS_CockpitSetDustCamera') if off(n) > 0]
    print(f'== {fn}: CODE {len(code)} bytes')
    for m in re.finditer(re.escape(call(setf)), code):
        p = m.start(); meth_name = meth[bisect.bisect_right(starts, p) - 1][1]
        seg = code[max(0, p - 60):p]; g = seg.rfind(call(getf))
        if g < 0: ops = 'no GetFlags within 60 bytes'
        else:
            t = seg[g + 5:]
            if len(t) >= 4 and t[-4] in (0x0d, 0x0f): t = t[:-4]  # camera push + argc byte
            ops = ' '.join(('|' if mm.group(5) == b'\x54' else '&' if mm.group(5) == b'\x53' else 'push ')
                           + hex(int.from_bytes(mm.group(2) or mm.group(3) or mm.group(4), 'big', signed=True) & 0xffffffff)
                           for mm in re.finditer(rb'(\x07(....)|\x06(..)|\x05(.))([\x53\x54\x32]?)', t, re.S))
        nxt = [(q, n) for o, n in cockpit for q in [code.find(call(o), p)] if 0 <= q < p + 90]
        tail = (' -> ' + min(nxt)[1]) if nxt else ''
        print(f'  CODE+{p} {meth_name}: {ops}{tail}')

def logs(paths):
    rx = re.compile(r'object_fade .*?flags270=([0-9a-f]+).*?near36c=([0-9a-f]+) far370=([0-9a-f]+) scale_bits=([0-9a-f]+) config768=([0-9a-f]+)')
    c = collections.Counter()
    for p in paths:
        with open(p, errors='replace') as f:
            for line in f:
                if 'object_fade' in line:
                    m = rx.search(line)
                    if m: c[m.group(1)] += 1
    print(f'object_fade rows in {len(paths)} logs')
    for fl, n in c.most_common():
        v = int(fl, 16)
        print(f'  flags270={fl} rows={n} 0x40000={int(bool(v & 0x40000))} fog0x10000={int(bool(v & 0x10000))}')

if __name__ == '__main__':
    exe_checks()
    a = s.Assets(Path(bob1.DEFAULT_GAME))
    for fn in ('l/x3story.obj', 'l/x3galedit.obj', 'l/x3intro.obj'): kc_sites(a, fn)
    if len(sys.argv) > 1: logs(sys.argv[1:])
