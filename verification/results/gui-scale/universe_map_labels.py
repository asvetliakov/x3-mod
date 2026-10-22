#!/usr/bin/env python3
"""Static evidence for docs/reverse-engineering/gui-scale.md section 7 (universe map sector labels under ui_scale).

Reads X3AP.exe and the addon catalogues read-only and prints derived facts only (addresses, decoded mnemonics,
names, counts, check results); no game or script bytes are written. No Wine, no game launch.

  python3 -I verification/results/gui-scale/universe_map_labels.py [game_root] > universe_map_labels.txt

Default game root /mnt/c/X3 (the native Windows install seen from WSL2).
"""
import hashlib, re, struct, subprocess, sys, bisect, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
from inspect_x3 import read_catalogue  # noqa: E402
from kc_obj import Obj, load_member  # noqa: E402

GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
EXE = GAME / 'X3AP.exe'
data = EXE.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
optsz = struct.unpack_from('<H', data, pe + 20)[0]
BASE = 0x400000
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    name = data[o:o + 8].rstrip(b'\0').decode()
    vs, va, rs, ro = struct.unpack_from('<IIII', data, o + 8)
    secs.append((name, va + BASE, min(vs, rs) if name == '.text' else max(vs, rs), ro))


def off(v):
    for _, va, sz, ro in secs:
        if va <= v < va + sz:
            return v - va + ro
    raise ValueError(hex(v))


def u32(v): return struct.unpack_from('<I', data, off(v))[0]
def cstr(v): o = off(v); return data[o:data.index(b'\0', o)].decode('latin1')


checks = {}
print('X3AP.exe sha256', hashlib.sha256(data).hexdigest())

# 1. Native names and the INS dispatcher cases (name table 0x0057aef0, jump table 0x0042f064).
NAMES, JT = 0x0057aef0, 0x0042f064
for idx in (0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43):
    print('INS case 0x%02x %-28s entry 0x%08x' % (idx, cstr(u32(NAMES + 4 * idx)), u32(JT + 4 * idx)))
checks['case40_is_BuildGalaxyMapPos'] = cstr(u32(NAMES + 0x100)) == 'INS_BuildGalaxyMapPos'
checks['case3e_is_GalaxyMapFindSector'] = cstr(u32(NAMES + 0xf8)) == 'INS_GalaxyMapFindSector'

# 2. Decode the two functions and the dispatcher cases with objdump.
def decode(lo, hi):
    o = off(lo)
    tmp = Path(tempfile.mkstemp(suffix='.bin')[1])
    tmp.write_bytes(data[o:o + (hi - lo)])
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '-Mintel', '--adjust-vma=0x%x' % lo, str(tmp)],
                         capture_output=True, text=True, check=True).stdout
    tmp.unlink()
    ins = []
    for line in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(.*)$', line)
        if m:
            ins.append((int(m.group(1), 16), bytes.fromhex(m.group(2).replace(' ', '')), m.group(3).strip()))
    return ins


fn_label = decode(0x0041e350, 0x0041eb20)        # BuildGalaxyMapPos worker (sector names)
fn_pick = decode(0x0041e030, 0x0041e2f0)         # GalaxyMapFindSector worker
disp = decode(0x0042e934, 0x0042e9ff)            # dispatcher case 0x3e
by = {va: (b, t) for va, b, t in fn_label + fn_pick + disp}


def show(lo, hi):
    for va, b, t in fn_label + disp:
        if lo <= va < hi:
            print('   0x%08x %-26s %s' % (va, b.hex(' ')[:26], t))


print('display record reads (real screen size, *(*0x00606f38)+4/+6) and camera viewport in the label worker:')
for va, b, t in fn_label:
    if ('0x606f38' in t) or re.search(r'word ptr \[e[a-d]x\+0x[46]\]', t) or re.search(r'\+0x(28[8c]|29[04]|30[04]|274)\]', t):
        print('   0x%08x %s' % (va, t))
print('same reads in the pick worker 0x0041e030:')
for va, b, t in fn_pick:
    if ('0x606f38' in t) or re.search(r'word ptr \[e[a-d]x\+0x[46]\]', t) or re.search(r'\+0x(28[8c]|29[04]|30[04])\]', t):
        print('   0x%08x %s' % (va, t))

# 3. The callback push and the proposed claim I (label values / s).
print('label callback tail 0x0041ea9f..0x0041ead2:'); show(0x0041ea9f, 0x0041ead2)
checks['callback_name'] = cstr(0x0055b63c) == 'WriteGalaxyMapSectorName'
checks['callback_push'] = by.get(0x0041eac3, (b'',))[0] == bytes.fromhex('683cb65500')
checks['callback_call'] = by.get(0x0041eacd, (b'', ''))[1].startswith('call') and '0x49f4c0' in by[0x0041eacd][1]
I_SPAN = (0x0041eab2, 0x0041eab8)
checks['I_bytes'] = by.get(0x0041eab2, (b'',))[0] == bytes.fromhex('8b0d64fc5700') and 0x0041eab8 in by
checks['I_first_value_pushed_at_0x0041eaa8'] = by.get(0x0041eaa8, (b'',))[0] == b'\x50'
checks['callee_0x0049f4c0_writes_flags_first'] = data[off(0x0049f4c0):off(0x0049f4c0) + 3] == bytes.fromhex('83ec0c')

# 4. The proposed claim J (map pick point * s) in case 0x3e.
print('dispatcher case 0x3e head:'); show(0x0042e934, 0x0042e970)
J_SPAN = (0x0042e952, 0x0042e959)
checks['J_bytes'] = by.get(0x0042e952, (b'',))[0] == bytes.fromhex('8b7606') and \
    by.get(0x0042e955, (b'',))[0] == bytes.fromhex('897c240c') and 0x0042e959 in by
checks['J_flags_live_from_cmp_0x0042e949'] = by.get(0x0042e949, (b'',))[0] == bytes.fromhex('393d08856000') and \
    by.get(0x0042e95d, (b'', ''))[1].startswith('je')

# 5. Span hygiene: whole instructions, one aligned qword for the 5-byte jump, no raw branch / dword into the span.
starts = set(by)
lo_t = next(va for n, va, sz, ro in secs if n == '.text'); sz_t = next(sz for n, va, sz, ro in secs if n == '.text')
text = data[off(lo_t):off(lo_t) + sz_t]
for name, (a, b) in (('I', I_SPAN), ('J', J_SPAN)):
    checks[f'{name}_boundaries'] = a in starts and b in starts
    checks[f'{name}_jump_in_one_qword'] = (a & ~7) == ((a + 4) & ~7)
    hits = []
    for i in range(len(text) - 6):
        op = text[i]
        if op in (0xe8, 0xe9): t = lo_t + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
        elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f: t = lo_t + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
        elif 0x70 <= op <= 0x7f or op == 0xeb or 0xe0 <= op <= 0xe3: t = lo_t + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
        else: continue
        if a < t < b: hits.append(lo_t + i)
    # a raw encoding whose source is itself a decoded instruction start inside the known functions would be real
    real = [h for h in hits if h in starts]
    print(f'claim {name} 0x{a:08x}..0x{b:08x}: raw rel8/rel32 encodings into the interior {len(hits)}, '
          f'of them at decoded instruction starts in the scanned functions {len(real)}')
    checks[f'{name}_no_decoded_branch_into_span'] = real == []
    refs = []
    for shift in range(4):
        for k in range(shift, len(data) - 3, 4):
            v = struct.unpack_from('<I', data, k)[0]
            if a < v <= b: refs.append(k)
    checks[f'{name}_no_dword_into_span'] = refs == []
    jt_inside = [hex(u32(JT + 4 * k)) for k in range(113) if a < u32(JT + 4 * k) < b]
    checks[f'{name}_no_jump_table_entry_inside'] = jt_inside == []
# every decoded direct branch in the two decoded ranges, none into either span
inner = []
for va, b_, t in fn_label + fn_pick + disp:
    m = re.match(r'(j\w+|call)\s+0x([0-9a-f]+)$', t)
    if m:
        tgt = int(m.group(2), 16)
        for a, b in (I_SPAN, J_SPAN):
            if a < tgt < b: inner.append((hex(va), hex(tgt)))
checks['no_decoded_direct_branch_into_I_or_J'] = inner == []

# 6. KC side: which story object loads, and the script consumers.
cats = sorted((GAME / 'addon').glob('[0-9][0-9].cat'))
winner = None
for cat in cats:
    if any(e['path'].lower() == 'l/x3story.obj' for e in read_catalogue(cat)): winner = cat
loose = [p for p in (GAME / 'L').glob('x3story*.obj')] if (GAME / 'L').is_dir() else []
print('loose L\\x3story*.obj:', [p.name for p in loose] or 'none',
      '| newest addon catalogue with L/x3story.obj:', winner.name if winner else None)
for label, cat in (('stock addon/04.cat', GAME / 'addon/04.cat'), ('loaded ' + str(winner.relative_to(GAME)), winner)):
    raw = load_member(cat); o = Obj(raw); keys = o.entries
    print(f'== {label}: sha256 {hashlib.sha256(raw).hexdigest()[:16]} build {o.build}')
    sp, sn = o.sections['SYMB']; sy = raw[sp + 8:sp + 8 + sn]
    m = re.search(rb'\x00WriteGalaxyMapSectorName\x00', o.s); nm = m.start() + 1
    entry = None
    for k in range(4, len(sy) - 12, 4):
        if struct.unpack_from('>I', sy, k + 4)[0] == nm:
            entry, _, loc, argc = struct.unpack_from('>IIII', sy, k); break
    print(f'  WriteGalaxyMapSectorName: global function, CODE 0x{entry:06x}, {argc} arguments')

    def natives_in(e, end):
        out = []
        pc = e
        while pc < end:
            if o.code[pc] == 0x82: out.append((pc, o.name(struct.unpack_from('>I', o.code, pc + 1)[0])))
            pc += o.width(pc)
        return out
    end = next(k for k in keys if k > entry)
    calls = natives_in(entry, end)
    print('  its native calls:', ', '.join(f'{n}@0x{pc:06x}' for pc, n in calls))
    # The texture argument of B3D_TexTextBlock is the first pushed constant after the 'push 8' argument count? Print
    # the constants pushed in the 40 bytes before the call instead (pushw operands).
    for pc, n in calls:
        if n == 'B3D_TexTextBlock':
            q, consts = entry, []
            while q < pc:
                if o.code[q] == 0x06 and pc - q < 48: consts.append(struct.unpack_from('>h', o.code, q + 1)[0])
                q += o.width(q)
            print(f'  pushw constants before B3D_TexTextBlock@0x{pc:06x}: {consts}')
            checks[f'{label}_label_into_texture_1236'] = 1236 in consts
    # callers of the galaxy natives
    for nat in ('INS_BuildGalaxyMap', 'INS_BuildGalaxyMapPos', 'INS_GalaxyMapFindSector'):
        mm = re.search(rb'\x00' + nat.encode() + rb'\x00', o.s); so = mm.start() + 1
        sites = [pc for pc in o.all_insns() if o.code[pc] == 0x82 and struct.unpack_from('>I', o.code, pc + 1)[0] == so]
        owners = ['%s@0x%06x' % (o.meth[keys[bisect.bisect_right(keys, pc) - 1]][0][1], pc) for pc in sites]
        print(f'  {nat}: {len(sites)} site(s) {owners}')

# 7. The measurement in the orchestrator's screenshot against the predicted error (s - 1) * offset.
s, pitch = 1.5, 94.7
for what, correct, k in (('3 columns left x', 231, -3), ('1 row down y', 620, 1)):
    print(f'predicted {what}: correct {correct} + (s-1)*{k}*{pitch} = {correct + (s - 1) * k * pitch:.0f}')

print('checks:', sum(checks.values()), '/', len(checks), 'pass')
for k, v in checks.items():
    if not v: print('  FAIL', k)
