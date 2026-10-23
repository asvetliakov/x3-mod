#!/usr/bin/env python3
"""Static pin of the instanced-batch path and the whole-call timer sites named in
docs/reverse-engineering/view-submit-hot-path.md ("Combat prepare span").
Checks, against X3AP.exe: expected bytes at every site, each E8 target, that each
site starts and ends on an instruction boundary of a linear sweep of .text, that
no direct branch/call lands strictly inside a site, and that no abs32 dword in the
file points strictly inside one. Prints one row per site and PASS/FAIL.
Usage: instanced_path_sites.py [X3AP.exe]  (needs i686-w64-mingw32-objdump)"""
import re, struct, subprocess, sys
exe = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/X3AP.exe'
# name, va, expected bytes, E8 target or None
SITES = [
    ('collect_call',      0x0047ea1b, 'e8d0e4feff', 0x0046cef0),  # per root node: instance collection
    ('submit_call',       0x0047eb7a, 'e801e5feff', 0x0046d080),  # per view-layer: instanced batch submit
    ('batch_draw_call',   0x0046d0a7, 'e8942c0500', 0x004bfd40),  # per batch material: Lock/copy/Unlock + effect draw
    ('instance_add_call', 0x0046d012, 'e849290500', 0x004bf960),  # per instance x sub-part: vertex transform
    ('texanim_trav',      0x0047e015, 'e8c6860700', 0x004f66e0),
    ('texanim_drain',     0x0047e722, 'e8b97f0700', 0x004f66e0),
    ('texanim_other',     0x0047c8bf, 'e81c9e0700', 0x004f66e0),
    ('wrapper_trav',      0x0047e076, 'e8456f0400', 0x004c4fc0),
    ('wrapper_drain',     0x0047e769, 'e852680400', 0x004c4fc0),
    ('cull_call',         0x0047e7a5, 'e836e8ffff', 0x0047cfe0),  # pinned by lens_flare_cull (0x0047e7a0, 10 bytes)
    ('lock_begin',        0x004bfdba, '6800200000', None),        # push D3DLOCK_DISCARD
    ('lock_end',          0x004bfde3, '8b430c8b5304', None),      # after the jl: memcpy argument loads
    ('unlock_begin',      0x004bfdfe, '8b038b088b5130', None),    # after memcpy: Unlock vtable load
    ('unlock_end',        0x004bfe0b, '8b7d143bfe', None),        # after Unlock: mov edi,[ebp+0x14]; cmp edi,esi
]
data = open(exe, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', data, o + 8)
    secs.append((0x400000 + va, vs, ro))
def off(v):
    for b, s, r in secs:
        if b <= v < b + s: return r + v - b
    raise ValueError(hex(v))
lst = subprocess.run(['i686-w64-mingw32-objdump', '-d', '-M', 'intel', '--no-show-raw-insn', exe],
                     capture_output=True, text=True, check=True).stdout
starts, targets = set(), []
for l in lst.splitlines():
    m = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', l)
    if not m: continue
    a = int(m.group(1), 16); starts.add(a)
    t = re.search(r'0x([0-9a-f]+)\s*$', m.group(3))
    if t and '[' not in m.group(3) and (m.group(2).startswith('j') or m.group(2) in ('call', 'loop')):
        targets.append((a, int(t.group(1), 16)))
abs32 = set()
for i in range(len(data) - 3):
    v = struct.unpack_from('<I', data, i)[0]
    if 0x401000 <= v < 0x532000: abs32.add(v)
ok = True
for name, va, hexb, tgt in SITES:
    exp = bytes.fromhex(hexb); n = len(exp)
    got = data[off(va):off(va) + n]
    bok = got == exp
    tok = True
    if tgt is not None:
        rel = struct.unpack_from('<i', got, 1)[0]
        tok = (va + 5 + rel) & 0xffffffff == tgt
    bnd = va in starts and va + n in starts
    inner = [hex(a) for a, t in targets if va < t < va + n]
    ab = [hex(v) for v in range(va + 1, va + n) if v in abs32]
    good = bok and tok and bnd and not inner and not ab
    ok &= good
    print(f'{name:18s} {va:#010x} len={n} bytes={"ok" if bok else got.hex()} target={"ok" if tok else "BAD"} '
          f'boundaries={"ok" if bnd else "BAD"} inner_edges={inner or 0} abs32_inside={ab or 0} {"PASS" if good else "FAIL"}')
print('ALL PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
