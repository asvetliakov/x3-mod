#!/usr/bin/env python3
"""Static evidence for field-of-view.md §8 (menu/config path) and §9 (chase distance).

Reads the installed X3AP.exe and addon/04.cat:L/x3story.obj (XOR 0x33) in memory and prints only
derived facts: CODE offsets, method names, member defaults, instruction bytes at named sites and
arithmetic. No script or code bytes are written anywhere.
KC instruction widths are those of the loader's byte-swap pass 0x0049e1a0 (opcode-5 -> class table
0x0049e464 -> handler table 0x0049e444): 05 +1, 06 +2, 07/0b/32..3a/49/4e/82/84..88 +4, 0a +8,
0d..0f/11..16/18..1d/1f..21/23/28/29/2c/30/48/4d/6e +2, 47/4c +1, 78 = +2+4*(n+2), 79 = +4+4*(2*(a+b)+1).
"""
import bisect, math, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/analysis'))
from inspect_x3 import read_catalogue

X3 = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'

# ---------- KC object ----------
def member(cat, name):
    for e in read_catalogue(cat):
        if e['path'].lower() == name.lower():
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return bytes(b ^ 0x33 for b in f.read(e['size']))
obj = member(X3 / 'addon/04.cat', 'L/x3story.obj')
code_len = struct.unpack_from('>I', obj, 0x10)[0]; code = obj[0x14:0x14 + code_len]
s = obj.find(b'STRG'); n = struct.unpack_from('>I', obj, s + 4)[0]; src = obj[s + 8:s + 8 + n]
strg = bytearray(n); strg[0] = ~src[0] & 255
for i in range(1, n): strg[i] = (~src[i] - src[i - 1]) & 255
c = obj.find(b'CLAS'); n = struct.unpack_from('>I', obj, c + 4)[0]; clas = obj[c + 8:c + 8 + n]
def sname(o): return bytes(strg[o:strg.find(b'\0', o)]).decode('latin1')
def soff(t): return re.search(rb'\x00' + re.escape(t) + rb'\x00', strg).start() + 1
W = {5: 1, 6: 2, 7: 4, 0x0a: 8, 0x0b: 4, 0x47: 1, 0x48: 2, 0x49: 4, 0x4c: 1, 0x4d: 2, 0x4e: 4, 0x6e: 2, 0x82: 4}
W.update({o: 4 for o in (0x84, 0x85, 0x86, 0x87, 0x88)})
W.update({o: 2 for o in list(range(0x0d, 0x10)) + list(range(0x11, 0x17)) + list(range(0x18, 0x1e)) + [0x1f, 0x20, 0x21, 0x23, 0x28, 0x29, 0x2c, 0x30]})
W.update({o: 4 for o in range(0x32, 0x3b)})
def width(at):
    op = code[at]
    if op == 0x78: return 3 + 4 * (struct.unpack_from('>H', code, at + 1)[0] + 2)
    if op == 0x79: a, b = struct.unpack_from('>HH', code, at + 1); return 5 + 4 * (2 * (a + b) + 1)
    return 1 + W.get(op, 0)

# Class records: header (id, parent, hash, nmethods), nmethods x (entry, name, frame, args),
# then nvars and nvars x (index, default value, type, hash).
def find_class(entry):
    i = clas.find(struct.pack('>I', entry))
    j = i
    while True:
        e, nm, fr, ar = struct.unpack_from('>4I', clas, j - 16)
        if not (0 < e < len(code) and fr < 512 and ar < 64): break
        j -= 16
    cid, parent, _, nmeth = struct.unpack_from('>4I', clas, j - 16)
    methods = [struct.unpack_from('>4I', clas, j + 16 * k) for k in range(nmeth)]
    p = j + 16 * nmeth; nvars = struct.unpack_from('>I', clas, p)[0]
    varsd = [struct.unpack_from('>4I', clas, p + 4 + 16 * k) for k in range(nvars)]
    return cid, methods, varsd
cid, methods, varsd = find_class(0x156e6)
mname = {e: sname(nm) for e, nm, _, _ in methods}
ents = sorted(mname)
print(f'class {cid:#x}: {len(methods)} methods {ents[0]:#x}..{ents[-1]:#x}, {len(varsd)} static members')
idx, val, typ, _ = varsd[0x16]
print(f'class {cid:#x} member {idx:#x}: default value {val} type {typ}  (GetFocus/SetFocus member)')
stores = []
for k, e in enumerate(ents):
    end = ents[k + 1] if k + 1 < len(ents) else e + 0x4000
    at = e
    while at < end:
        if code[at] == 0x16 and struct.unpack_from('>H', code, at + 1)[0] == 0x16: stores.append((mname[e], hex(at)))
        at += width(at)
print('STOREM 0x16 inside class 0x96 methods:', stores)
setf = [e for e in ents if mname[e] == 'SetFocus'][0]; getf = [e for e in ents if mname[e] == 'GetFocus'][0]
calls = lambda e: [hex(m.start()) for op in (b'\x86', b'\x88') for m in re.finditer(re.escape(op + struct.pack('>I', e)), code)]
print(f'SetFocus {setf:#x} callers (86/88):', calls(setf), '; by name:',
      [hex(m.start()) for m in re.finditer(re.escape(b'\x85' + struct.pack('>I', soff(b'SetFocus'))), code)])
print(f'GetFocus {getf:#x} callers (86/88):', calls(getf))
print('INS_SetFocus native call sites:', [hex(m.start()) for m in re.finditer(re.escape(b'\x82' + struct.pack('>I', soff(b'INS_SetFocus'))), code)])
print('persisted system natives:', sorted(set(m.group().decode() for m in re.finditer(rb'P_[GS]etSys[A-Za-z0-9]*', strg))))
def switch_cases(at):
    a, b = struct.unpack_from('>HH', code, at + 1); w = struct.unpack_from('>%dI' % (2 * (a + b) + 1), code, at + 5)
    return {w[1 + 2 * k]: w[2 + 2 * k] for k in range(a)}
for at in (0x115b64, 0x115108, 0x1154e2):
    assert code[at] == 0x79
    print(f'menu switch at CODE {at:#x}: line 0x2406 -> {switch_cases(at).get(0x2406, 0):#x}')
# The StartMonitor external boom: INS_CockpitSetViewCameraOffset(cockpit, 0, m1c, -m1b)
for nat in (b'INS_CockpitSetViewCameraOffset', b'SA_GetTotalSize', b'SE_LinFunc'):
    sites = [m.start() for m in re.finditer(re.escape(b'\x82' + struct.pack('>I', soff(nat))), code)]
    print(nat.decode(), 'sites in 0xf0000..0xf5000:', [hex(x) for x in sites if 0xf0000 <= x < 0xf5000])

# ---------- EXE ----------
d = (X3 / 'X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', d, 0x3c)[0]; ns = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
for i in range(ns):
    o = pe + 24 + opt + 40 * i
    if d[o:o + 5] == b'.text': va = struct.unpack_from('<I', d, o + 12)[0] + 0x400000; raw = struct.unpack_from('<I', d, o + 20)[0]; size = struct.unpack_from('<I', d, o + 16)[0]
text = d[raw:raw + size]
def at(a, l): return text[a - va:a - va + l]
jt = [struct.unpack_from('<I', at(0x0042f064 + 4 * k, 4))[0] for k in range(0x71)] if False else None
# jump table lives in .text right after the dispatcher
tab = [struct.unpack_from('<I', text, 0x0042f064 - va + 4 * k)[0] for k in range(0x71)]
print(f'dispatcher 0x0042d340 table 0x0042f064: case 0x21 -> {tab[0x21]:#010x}, case 0x36 -> {tab[0x36]:#010x}')
for a, l, what in ((0x0042dbed, 3, 'MOV EAX,[EBP+0x18]'), (0x0042dbf0, 3, 'MOV ECX,[EAX+1]'), (0x0042dbf3, 5, 'MOV EAX,[0x006085e4]'),
                   (0x0042dbf8, 6, 'MOV EDX,[0x00608504]  <- proposed call site'), (0x0042dbfe, 2, 'PUSH 0'), (0x0042dc00, 1, 'PUSH EAX'),
                   (0x0042dc01, 3, 'MOV EAX,[EBP+0xc]'), (0x0042dc04, 3, 'MOV [EDX+0x24],ECX'), (0x0042dc07, 5, 'CALL 0x004a47f0'),
                   (0x0042e1e9, 6, 'MOV [EAX+0x160],ECX (case 0x36)'), (0x00420c9f, 5, 'CALL 0x004f0da0 (+0x160 x camera basis)'),
                   (0x00420dda, 3, 'MOV EAX,[EBX+0x58]')):
    print(f'{a:#010x} {at(a, l).hex(" "):20s} {what}')
print('0x0042dbf8 qword-aligned:', 0x0042dbf8 % 8 == 0, '; span end 0x0042dbfd inside qword:', (0x0042dbfd // 8) == (0x0042dbf8 // 8))
hits = []
for i in range(len(text) - 6):
    b = text[i]
    if b in (0xe8, 0xe9): t = va + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
    elif b == 0x0f and 0x80 <= text[i + 1] <= 0x8f: t = va + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
    elif b == 0xeb or 0x70 <= b <= 0x7f: t = va + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
    else: continue
    if 0x0042dbee <= t <= 0x0042dc0b: hits.append((hex(va + i), hex(t)))
print('byte-pattern branches into 0x0042dbee..0x0042dc0b:', hits,
      '; jump-table entries into it:', [hex(k) for k, v in enumerate(tab) if 0x0042dbee <= v <= 0x0042dc0b],
      '; absolute dword refs:', sum(d.count(struct.pack('<I', a)) for a in range(0x0042dbee, 0x0042dc0c)))

# Every access to registry+0x24 through the global registry pointer (a register-passed registry is not traced).
import capstone
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
acc = []; pat = struct.pack('<I', 0x00608504); i = text.find(pat); nref = 0
while i != -1:
    ins = None
    for back in (1, 2):
        cand = next(md.disasm(text[i - back:i - back + 10], va + i - back), None)
        if cand and '[0x608504]' in cand.op_str and cand.address + cand.size >= va + i + 4: ins = cand; break
    if ins:
        nref += 1
        m = re.match(r'mov (e[a-z]{2}), dword ptr \[0x608504\]', ins.mnemonic + ' ' + ins.op_str)
        if m:
            reg = m.group(1)
            for j, x in enumerate(md.disasm(text[ins.address - va + ins.size:ins.address - va + ins.size + 80], ins.address + ins.size)):
                t = x.mnemonic + ' ' + x.op_str
                if j >= 14 or x.mnemonic in ('ret', 'jmp', 'call') or re.match(r'(mov|lea|pop|xor) ' + reg + r'\b', t): break
                if '[' + reg + ' + 0x24]' in t: acc.append(f'{ins.address:#010x}->{x.address:#010x} {t}')
    i = text.find(pat, i + 1)
print(f'loads of [0x00608504]: {nref}; followed by a +0x24 access: {acc}')
print('registry constructor store 0x0041c9d9:', at(0x0041c9d9, 7).hex(' '))

# ---------- arithmetic ----------
def deg(F): return F * 360 / 65536
def vert(F): return math.degrees(2 * math.atan(0.75 * math.tan(F * math.pi / 65536)))
def h169(F): return math.degrees(2 * math.atan(0.75 * 16 / 9 * math.tan(F * math.pi / 65536)))
F_user = 0x3470
k = math.tan(F_user * math.pi / 65536)
print(f'F_user=0x{F_user:04x}: tan(F/2)={k:.6f}; ship screen-size ratio vs 0x4000 = {1 / k:.4f}; chase distance factor 0.75/half_vfov_tan = {0.75 / (0.75 * k):.4f}')
print('remap tan(F\'/2) = 0.75*tan(F/2) ("N degrees horizontal on 16:9"); F = (N<<16)//360 as SetFocus computes it')
print(' menu N | vanilla F   vert    h16:9 | mapped F    vert    h16:9 | sun x-test z_crit at 32:9 (W=174762/65536)')
Wf = ((0xC000 * 5120) // 1440) / 65536.0
for dd in list(range(70, 101)):
    Fm = (dd << 16) // 360
    Fp = round(65536 / math.pi * math.atan(math.tan(Fm * math.pi / 65536) * k))
    zc = 2**32 / (Wf * math.tan(Fp * math.pi / 65536))
    print(f' {dd:6d} | 0x{Fm:04x} {vert(Fm):7.3f} {h169(Fm):8.3f} | 0x{Fp:04x} {vert(Fp):7.3f} {h169(Fp):8.3f} | {zc:.4g}{"" if zc < 2**31 else " (>= 2^31: never)"}')
