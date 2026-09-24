#!/usr/bin/env python3
"""Static evidence for field-of-view.md §7.4 (the savegame restores registry+0x24).

Reads the installed X3AP.exe, the bottle's savegames (gzip, in memory) and addon/04.cat:L/x3story.obj
(XOR 0x33, in memory). Prints only derived facts: addresses, instruction bytes at named sites, store
lists, jump-table entries, the registry focus field of each savegame, KC call sites and arithmetic.
No code, script or save bytes are written anywhere.
"""
import bisect, gzip, math, re, struct, sys
from pathlib import Path
import capstone
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'

# ---------- EXE ----------
d = (X3 / 'X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]; ns = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(ns):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ro = struct.unpack_from('<4I', d, o + 8); secs.append((d[o:o + 8].rstrip(b'\0'), va + 0x400000, vs, ro, rs))
    if d[o:o + 5] == b'.text': tva, traw, tsize = va + 0x400000, ro, rs
text = d[traw:traw + tsize]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def image(v, n):
    for _, va, vs, ro, rs in secs:
        if va <= v < va + max(vs, rs): return d[ro + v - va:ro + v - va + n]
def cstr(v): b = image(v, 64); return b[:b.find(b'\0')].decode('latin1')
def listing(a, end):
    return [f'{x.address:#010x} {x.bytes.hex(" "):30s} {x.mnemonic} {x.op_str}' for x in md.disasm(text[a - tva:end - tva], a)]
def calls_to(t):
    return [hex(tva + i) for i in range(len(text) - 5) if text[i] == 0xe8 and tva + i + 5 + struct.unpack_from('<i', text, i + 1)[0] == t]
def branches_into(lo, hi):
    hits = []
    for i in range(len(text) - 6):
        b = text[i]
        if b in (0xe8, 0xe9): t = tva + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
        elif b == 0x0f and 0x80 <= text[i + 1] <= 0x8f: t = tva + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
        elif b == 0xeb or 0x70 <= b <= 0x7f: t = tva + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
        else: continue
        if lo <= t <= hi: hits.append((hex(tva + i), hex(t)))
    return hits
def stores(disp):
    """Every instruction whose destination is [reg+disp] (reg not ESP/EBP-frame), by anchoring on the disp32 bytes."""
    out, seen, pat = [], set(), struct.pack('<I', disp)
    i = text.find(pat)
    while i != -1:
        for back in range(2, 8):
            x = next(md.disasm(text[i - back:i - back + 16], tva + i - back), None)
            if x and x.address <= tva + i - 2 and x.address + x.size > tva + i + 3 and f'+ {disp:#x}]' in x.op_str:
                dst = x.op_str.split(',')[0]
                if x.address not in seen and f'+ {disp:#x}]' in dst and 'esp' not in dst and x.mnemonic in ('mov', 'and', 'or', 'add', 'sub', 'inc', 'dec', 'xor'):
                    seen.add(x.address); out.append(f'{x.address:#010x} {x.bytes.hex(" ")}  {x.mnemonic} {x.op_str}')
                break
        i = text.find(pat, i + 1)
    return out

print('== 1. writers (whole .text, disp32 anchored) ==')
for disp, what in ((0x298, 'camera focus +0x298 (also other classes)'), (0x230, 'cockpit +0x230'), (0x1c0, 'cockpit connect mode +0x1c0')):
    print(f'-- stores to [reg+{disp:#x}] ({what}):'); [print('  ', s) for s in stores(disp)]
imm = [s for s in (f'{x.address:#010x} {x.bytes.hex(" ")} {x.mnemonic} {x.op_str}' for m in re.finditer(rb'\xc7[\x40-\x47\x80-\x87]', text)
       for x in [next(md.disasm(text[m.start():m.start() + 12], tva + m.start()), None)] if x and x.op_str.endswith('0x4000') and '+ 0x24]' in x.op_str)]
print('-- MOV [reg+0x24],0x4000 anywhere:', imm)
print('-- stores to the registry slot [0x00608504]:', [hex(tva + m.start()) for m in re.finditer(rb'(\x89[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]|\xa3)\x04\x85\x60\x00', text)])
print('-- registry constructor callers:', calls_to(0x0041c960), '; registry serializer 0x0041c6e0 callers:', calls_to(0x0041c6e0),
      '; INS-section loader 0x0041f720 callers:', calls_to(0x0041f720), '; cockpit ctor callers:', calls_to(0x0041f8d0))
print('-- registry serializer, save reads of +0x24:'); [print('  ', l) for l in listing(0x0041c7c9, 0x0041c7d3) + listing(0x0041c8de, 0x0041c8e8)]
print('-- registry serializer, load tail (the store of the saved focus):'); [print('  ', l) for l in listing(0x0041c8b4, 0x0041c8cb)]
print('-- load order in 0x00404cc0 / 0x0041f720:'); [print('  ', l) for l in listing(0x004050f4, 0x004050f9) + listing(0x00405112, 0x00405117) + listing(0x0041f790, 0x0041f795) + listing(0x0041f83b, 0x0041f840) + listing(0x0041f854, 0x0041f859)]
print('-- cockpit ctor copy, per-frame apply, connect-mode load:'); [print('  ', l) for l in listing(0x0041fd95, 0x0041fd9e) + listing(0x0042114e, 0x00421151) + listing(0x00421588, 0x004215a0) + listing(0x00419f12, 0x00419f18)]
b3d = 0x0049679c; ins = 0x0042f064
b3d_names = 0x0057a420; ins_names = 0x0057aef0
for base, tab, k in ((b3d_names, b3d, 0x3d), (ins_names, ins, 0x05), (ins_names, ins, 0x06), (ins_names, ins, 0x07), (ins_names, ins, 0x21), (ins_names, ins, 0x2c)):
    print(f'-- case {k:#x} {cstr(struct.unpack("<I", image(base + 4 * k, 4))[0])} -> {struct.unpack_from("<I", text, tab - tva + 4 * k)[0]:#010x}')
[print('  ', l) for l in listing(0x00494f1e, 0x00494f3f)]
[print('  ', l) for l in listing(0x0042d4f4, 0x0042d50f)]
print('-- connect setter 0x00422cd0 callers:', calls_to(0x00422cd0), '; mode reset 0x004228a8 reached from', branches_into(0x004228a8, 0x004228a8))

print('== 4. proposed load-site claim 0x0041c8c1 (6 bytes) ==')
print('bytes:', text[0x0041c8c1 - tva:0x0041c8c7 - tva].hex(' '), '; jmp span 0x0041c8c1..0x0041c8c5 in qword 0x0041c8c0:', 0x0041c8c1 // 8 == 0x0041c8c5 // 8)
print('byte-pattern branches into 0x0041c8c2..0x0041c8c6:', branches_into(0x0041c8c2, 0x0041c8c6),
      '; absolute dword refs to 0x0041c8c1..0x0041c8c8:', sum(d.count(struct.pack('<I', a)) for a in range(0x0041c8c1, 0x0041c8c9)))
print('caller after the load call:'); [print('  ', l) for l in listing(0x0041f790, 0x0041f7ac)]
print('0x0048cdc0 entry (EDX written before read):'); [print('  ', l) for l in listing(0x0048cdc0, 0x0048cdcf)]
print('== 4b. optional second claim 0x0041a55c (cockpit +0x230 restored by the cockpit serializer 0x00419430) ==')
[print('  ', l) for l in listing(0x0041a54a, 0x0041a56e)]
print('save side of the same field:'); [print('  ', l) for l in listing(0x0041b7eb, 0x0041b7f1)]
print('byte-pattern branches into 0x0041a55d..0x0041a561:', branches_into(0x0041a55d, 0x0041a561),
      '; absolute dword refs:', sum(d.count(struct.pack('<I', a)) for a in range(0x0041a55c, 0x0041a563)),
      '; jmp span in one qword:', 0x0041a55c // 8 == 0x0041a560 // 8)
print('other [ebp+0x150/0x1c0/0x230/0x298] accesses in 0x00419430..0x0041c6e0:')
for x in md.disasm(text[0x00419430 - tva:0x0041c6e0 - tva], 0x00419430):
    if re.search(r'\[ebp \+ 0x(150|1c0|230|298)\]', x.op_str): print(f'   {x.address:#010x} {x.mnemonic} {x.op_str}')

# ---------- savegames ----------
print('== 2. savegames: registry section "INS " (BE32 fields in 0x0041c6e0 load order) ==')
for f in sorted((X3 / 'save').glob('*.sav')):
    b = gzip.decompress(f.read_bytes())
    for m in re.finditer(rb'INS \x00\x00\x00\x01', b):
        p = m.start() + 8 + 12 + 2 + 8 + 2 + 2 + 4 + 4
        tail = b[p + 4:p + 12]
        print(f'{f.name}: +0x24 = {struct.unpack_from(">I", b, p)[0]:#06x}; followed by count {struct.unpack_from(">I", tail)[0]} and tag {tail[4:8]!r}')

# ---------- arithmetic ----------
print('== 4. vanilla vs remapped focus values ==')
def remap(F): return int(math.floor(65536 / math.pi * math.atan(0.75 * math.tan(F * math.pi / 65536)) + 0.5))
V = {(N << 16) // 360: N for N in range(0, 181)}; T = {remap((N << 16) // 360): N for N in range(50, 131)}
print('table values equal to a vanilla (N<<16)/360, N 0..180:', sorted(set(V) & set(T)), '; min distance', min(abs(t - v) for t in T for v in V))
stub = lambda F: F if not 0x2334 <= F <= 0x5ccc else remap((((F * 360 + 0x8000) >> 16) << 16) // 360)
print('nearest-N remap applied to an already remapped value:', {hex(remap(0x4000)): hex(stub(remap(0x4000))), hex(remap((100 << 16) // 360)): hex(stub(remap((100 << 16) // 360)))})

# ---------- KC ----------
print('== 3. KC: every B3D_CameraSetFocus call and every 0x4000 push that reaches a focus call ==')
def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as fh:
                fh.seek(e['offset']); return bytes(x ^ 0x33 for x in fh.read(e['size']))
obj = member(X3 / 'addon/04.cat', 'L/x3story.obj')
code = obj[0x14:0x14 + struct.unpack_from('>I', obj, 0x10)[0]]
s = obj.find(b'STRG'); n = struct.unpack_from('>I', obj, s + 4)[0]; src = obj[s + 8:s + 8 + n]
strg = bytearray(n); strg[0] = ~src[0] & 255
for i in range(1, n): strg[i] = (~src[i] - src[i - 1]) & 255
c = obj.find(b'CLAS'); n = struct.unpack_from('>I', obj, c + 4)[0]; clas = obj[c + 8:c + 8 + n]
def sname(o): return bytes(strg[o:strg.find(b'\0', o)]).decode('latin1')
ent = {}
for i in range(len(clas) - 16):
    e, nm, fr, ar = struct.unpack_from('>4I', clas, i)
    if 0 < e < len(code) and fr < 512 and ar < 64 and 0 < nm < len(strg) and strg[nm - 1] == 0:
        t = sname(nm)
        if 1 < len(t) < 60 and t.isprintable(): ent.setdefault(e, t)
keys = sorted(ent)
def owner(a): e = keys[bisect.bisect_right(keys, a) - 1]; return f'{ent[e]}@{e:#x}'
W = {5: 1, 6: 2, 7: 4, 0x0a: 8, 0x0b: 4, 0x47: 1, 0x48: 2, 0x49: 4, 0x4c: 1, 0x4d: 2, 0x4e: 4, 0x6e: 2, 0x82: 4}
W.update({o: 4 for o in (0x84, 0x85, 0x86, 0x87, 0x88)})
W.update({o: 2 for o in list(range(0x0d, 0x10)) + list(range(0x11, 0x17)) + list(range(0x18, 0x1e)) + [0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30]})
W.update({o: 4 for o in range(0x32, 0x3b)})
def width(at):
    op = code[at]
    if op == 0x78: return 3 + 4 * (struct.unpack_from('>H', code, at + 1)[0] + 2)
    if op == 0x79: a, b = struct.unpack_from('>HH', code, at + 1); return 5 + 4 * (2 * (a + b) + 1)
    return 1 + W.get(op, 0)
def ops(e, end):
    at, out = e, []
    while at < end: out.append((at, code[at], code[at + 1:at + width(at)])); at += width(at)
    return out
def operand(op, arg):
    if op in (0x0d, 0x0f): return f'{"local" if op == 0x0d else "member"} {struct.unpack(">H", arg)[0]:#x}'
    if op == 6: return f'const {struct.unpack(">H", arg)[0]:#x}'
    if op == 7: return f'const {struct.unpack(">I", arg)[0]:#x}'
    if op == 0x82: return sname(struct.unpack('>I', arg)[0]) + '()'
    return f'op {op:#x}'
focus_names = {'B3D_CameraSetFocus'}  # INS_SetFocus: one site, SetFocus@0x156e6 (kc_fov_calls.txt)
for k, e in enumerate(keys):
    end = keys[k + 1] if k + 1 < len(keys) else len(code)
    try: seq = ops(e, end)
    except (IndexError, struct.error): continue
    for j, (at, op, arg) in enumerate(seq):
        if op == 0x82 and sname(struct.unpack('>I', arg)[0]) in focus_names:
            # two arguments pushed before the argc push: [.., value, camera, argc]
            val, cam = seq[j - 3], seq[j - 2]
            print(f'CODE {at:#08x} {sname(struct.unpack(">I", arg)[0])} in {owner(at)}: camera = {operand(cam[1], cam[2])}, focus = {operand(val[1], val[2])}')
print('ShowSpace camera roles: member 3 -> INS_CockpitSetSectorCamera (cockpit member 1), member 4 -> INS_CockpitSetGalaxyCamera,'
      ' member 5 -> B3D_CameraSetEnvironmentSource(member 5, member 4) with aspect 0x10000/0x10000 and focus 0x4000 (see §7.4)')
