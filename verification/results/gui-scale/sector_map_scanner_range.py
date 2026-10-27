#!/usr/bin/env python3
"""Static evidence for docs/reverse-engineering/sector-map-scanner-range.md (sector map scanner disc under text density).

Reads X3AP.exe and the addon catalogues read-only and prints derived facts only (addresses, decoded mnemonics, names,
constants, counts, emulation results); no game or script bytes are written. No Wine, no game launch.

  python3 -I verification/results/gui-scale/sector_map_scanner_range.py [game_root] > sector_map_scanner_range.txt

The emulation section ports the filled-ellipse rasteriser 0x004ef620 instruction for instruction (32-bit wrapping
decision variables, 16-bit coordinates) and compares it with the same algorithm in unbounded integers.
"""
import bisect, hashlib, math, re, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
from inspect_x3 import read_catalogue  # noqa: E402
from kc_obj import Obj, load_member  # noqa: E402

GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
data = (GAME / 'X3AP.exe').read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec, optsz = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', data, o + 8)
    secs.append((data[o:o + 8].rstrip(b'\0').decode(), va + 0x400000, min(vs, rs), ro))
TEXT = next(s for s in secs if s[0] == '.text')


def off(v):
    for _, va, sz, ro in secs:
        if va <= v < va + sz: return v - va + ro
    raise ValueError(hex(v))


def u32(v): return struct.unpack_from('<I', data, off(v))[0]
def cstr(v): o = off(v); return data[o:data.index(b'\0', o)].decode('latin1')
def raw(v, n): return data[off(v):off(v) + n]


def decode(lo, hi):
    tmp = Path(tempfile.mkstemp(suffix='.bin')[1]); tmp.write_bytes(raw(lo, hi - lo))
    out = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '-Mintel', '--adjust-vma=0x%x' % lo, str(tmp)],
                         capture_output=True, text=True, check=True).stdout
    tmp.unlink(); ins = []
    for line in out.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\t([0-9a-f ]+?)\s*\t(.*)$', line)
        if m: ins.append((int(m.group(1), 16), bytes.fromhex(m.group(2).replace(' ', '')), m.group(3).strip()))
    return ins


checks = {}
print('X3AP.exe sha256', hashlib.sha256(data).hexdigest())

# 1. KC native 121 and its dispatcher case.
NAMES, JT = 0x0057a420, 0x0049679c
for idx in (104, 109, 120, 121):
    print('KC native %3d %-20s case 0x%08x' % (idx, cstr(u32(NAMES + 4 * idx)), u32(JT + 4 * idx)))
checks['native121_TexCircleFill'] = cstr(u32(NAMES + 4 * 121)) == 'B3D_TexCircleFill' and u32(JT + 4 * 121) == 0x004962e2
case = {va: (b, t) for va, b, t in decode(0x004962e2, 0x0049631a)}
print('case 121:'); [print('   0x%08x %s' % (va, t)) for va, (b, t) in sorted(case.items())]
checks['case121_operands_dword_stride5'] = all(any(f'[eax+0x{k:x}]' in t for b, t in case.values()) for k in (1, 6, 0xb, 0x10, 0x15, 0x1a))
checks['case121_calls_0x0048bee0'] = case.get(0x004962fd, (b'', ''))[1] == 'call   0x48bee0'

# 2. 0x0048bee0: reject test in layout units, MPF_FONTSCALE scale block, call into the rasteriser.
fn = {va: (b, t) for va, b, t in decode(0x0048bee0, 0x0048c08c)}
print('0x0048bee0 (id >= 0 path):'); [print('   0x%08x %s' % (va, t)) for va, (b, t) in sorted(fn.items()) if va >= 0x0048bfbc]
checks['scale_block_0x48c04f'] = (fn[0x0048c045][1].startswith('test') and '0x10000' in fn[0x0048c045][1]
                                  and '0x784' in fn[0x0048c054][1] and fn[0x0048c05c][1].startswith('imul   edi')
                                  and fn[0x0048c05f][1].startswith('imul   ebx') and fn[0x0048c062][1].startswith('imul   eax,esi'))
checks['reject_test_before_scaling'] = all(a < 0x0048c045 for a in (0x0048bfd8, 0x0048bfe8, 0x0048bff7, 0x0048c007))
checks['call_rasteriser_0x48c076'] = fn[0x0048c076][1] == 'call   0x4ef620'
checks['remote_record_branch_unscaled'] = fn[0x0048c021][1] == 'call   0x475800' and 0x0048c021 < 0x0048c045

# 3. 0x004ef620: 16-bit coordinates, 32-bit decision variables.
ras = decode(0x004ef620, 0x004efa3c); rb = {va: (b, t) for va, b, t in ras}
print('0x004ef620 key instructions:')
for va in (0x004ef62a, 0x004ef632, 0x004ef635, 0x004ef6bc, 0x004ef70b, 0x004ef72d, 0x004ef740, 0x004ef74a, 0x004ef761,
           0x004ef766, 0x004ef7aa, 0x004ef7ec, 0x004ef811, 0x004ef871, 0x004ef884, 0x004ef89e, 0x004ef8a7, 0x004ef8af,
           0x004ef8b1, 0x004ef8cb, 0x004ef8d7, 0x004ef8e7, 0x004ef9be, 0x004ef9d8, 0x004efa11, 0x004efa1b):
    print('   0x%08x %s' % (va, rb[va][1]))
imuls32 = [va for va, b, t in ras if t.startswith('imul') and not re.search(r'\b[a-d]x\b|\bsi\b|\bdi\b', t)]
print('   32-bit imul in the routine:', len(imuls32))
checks['r_is_short'] = rb[0x004ef62a][1] == 'movsx  esi,ax'
checks['decision_sign_test_32bit'] = rb[0x004ef7a8][1] == 'test   ebp,ebp' and rb[0x004ef7aa][1].startswith('jl')
checks['region1_loop_cmp_32bit'] = rb[0x004ef8af][1] == 'cmp    edx,ecx' and rb[0x004ef8b1][1].startswith('jg')
checks['region2_sign_test_32bit'] = rb[0x004ef9be][1] == 'test   esi,esi'
checks['row_test_unsigned16'] = 'cx,WORD PTR [edi+0x6]' in rb[0x004ef7ec][1] and rb[0x004ef7f0][1].startswith('jae')
checks['unknown_blittype_string'] = cstr(0x00564b74).startswith('FilledCircle: Unknown Blittype')

# 4. Callers and the entry as a claim site.
t0, tl = TEXT[1], TEXT[2]; text = raw(t0, tl)


def rel_targets(lo, hi):
    hits = []
    for i in range(len(text) - 6):
        op = text[i]
        if op in (0xe8, 0xe9): t = t0 + i + 5 + struct.unpack_from('<i', text, i + 1)[0]
        elif op == 0x0f and 0x80 <= text[i + 1] <= 0x8f: t = t0 + i + 6 + struct.unpack_from('<i', text, i + 2)[0]
        elif 0x70 <= op <= 0x7f or op == 0xeb: t = t0 + i + 2 + struct.unpack_from('<b', text, i + 1)[0]
        else: continue
        if lo <= t < hi: hits.append((t0 + i, t))
    return hits


calls_ras = [hex(a) for a, t in rel_targets(0x004ef620, 0x004ef621) if text[a - t0] == 0xe8]
calls_fn = [hex(a) for a, t in rel_targets(0x0048bee0, 0x0048bee1) if text[a - t0] == 0xe8]
dword_refs = [k for k in range(0, len(data) - 3) if struct.unpack_from('<I', data, k)[0] == 0x004ef620]
into = [(hex(a), hex(t)) for a, t in rel_targets(0x004ef621, 0x004ef627)]
print('direct calls to 0x004ef620:', calls_ras, '| to 0x0048bee0:', calls_fn, '| image dwords = 0x004ef620:', len(dword_refs))
print('rel8/rel32 encodings anywhere in .text targeting 0x004ef621..0x004ef626:', len(into))
print('entry bytes 0x004ef620..0x004ef627:', raw(0x004ef620, 8).hex(' '), '|', rb[0x004ef620][1], '/', rb[0x004ef623][1], '/', rb[0x004ef627][1])
checks['rasteriser_single_caller'] = calls_ras == ['0x48c076'] and dword_refs == []
checks['entry_two_whole_insns_7_bytes'] = raw(0x004ef620, 7) == bytes.fromhex('83ec4c8b442458') and 0x004ef627 in rb
checks['entry_jump_in_one_qword'] = (0x004ef620 & 7) + 5 <= 8
checks['call_site_straddles_qword'] = (0x0048c076 & 7) + 5 > 8
checks['no_branch_into_entry_window'] = into == []

# 5. KC side: the single TexCircleFill site and its operands, the map window size.
for label, cat in (('stock addon/04.cat', GAME / 'addon/04.cat'), ('Mayhem addon/07.cat', GAME / 'addon/07.cat')):
    rawobj = load_member(cat); o = Obj(rawobj); keys = o.entries
    so = re.search(rb'\x00B3D_TexCircleFill\x00', o.s).start() + 1
    sites = [pc for pc in o.all_insns() if o.code[pc] == 0x82 and struct.unpack_from('>I', o.code, pc + 1)[0] == so]
    owners = ['%d::%s@0x%06x' % (o.meth[keys[bisect.bisect_right(keys, pc) - 1]][0][:2] + (pc,)) for pc in sites]
    pc = sites[0]; e = keys[bisect.bisect_right(keys, pc) - 1]
    ins = [q for q in o.insns(e) if q <= pc]
    window = [q for q in ins if pc - q < 0x200]
    consts = [o.operand(q) for q in window if o.code[q] in (0x06, 0x07)]
    print(f'== {label}: sha256 {hashlib.sha256(rawobj).hexdigest()[:16]} build {o.build}; TexCircleFill sites {owners}')
    print('   pushw/pushd constants in the 0x200 bytes before the call:', consts)
    print('   the six operands as pushed (last 10 insns):', o.decoded(window[-10], pc + 5 - window[-10]))
    checks[f'{label}_one_site_in_2216_SpecialUpdate'] = len(sites) == 1 and owners[0].startswith('2216::SpecialUpdate')
    checks[f'{label}_texture_1236_pushed'] = '1236' in consts
    checks[f'{label}_aspect_65536_last_push'] = o.operand(window[-7]) == '65536' and o.code[window[-7]] == 0x07
    gw = o.find(2216, 'GetMenuMaxWidth')[0]
    gwc = [o.operand(q) for q in o.insns(gw) if o.code[q] == 0x06]
    print('   2216::GetMenuMaxWidth pushw constants', gwc)
    checks[f'{label}_menu_max_512_128_1024'] = gwc[:4] == ['512', '512', '128', '512'] and '1024' in gwc
    other = [n for n in ('B3D_TexCircle',) if re.search(rb'\x00' + n.encode() + rb'\x00', o.s)]
    if other:
        so2 = re.search(rb'\x00B3D_TexCircle\x00', o.s).start() + 1
        n2 = sum(1 for q in o.all_insns() if o.code[q] == 0x82 and struct.unpack_from('>I', o.code, q + 1)[0] == so2)
        print('   B3D_TexCircle sites:', n2)


# 6. Emulation of 0x004ef620.
def i32(v): v &= 0xffffffff; return v - (1 << 32) if v & 0x80000000 else v
def s16(v): v &= 0xffff; return v - 65536 if v & 0x8000 else v
def u16(v): return v & 0xffff


def circlefill(cx, cy, r, W, H, wrap=True, asp=65536):
    w = i32 if wrap else (lambda v: v)
    spans = {}

    def span(x0, y, x1):
        y = s16(y); a, b = spans.get(y, (10 ** 9, -10 ** 9)); spans[y] = (min(a, x0), max(b, x1))
    rr = s16(r); A = w(rr * rr)
    clip = (cx + rr >= W) or (cx - rr < 0) or (cy + rr >= H) or (cy - rr < 0)
    B = int(A / (((asp * asp + 0x8000) >> 16) * 1.52587890625e-05)); twoB = w(2 * B)
    d = w(w((1 - B) * w(2 * A)) + w(w(w((rr - 1) * rr) * twoB) + B))
    Y, X = u16(r), 0

    def emit(left, right, Yv):
        if clip:
            l = max(s16(left), 0); rt = s16(right); rt = W - 1 if rt >= W else rt
            if l > rt: return
            if u16(Yv + cy) < H: span(l, Yv + cy, rt)
            if u16(cy - Yv) < H: span(l, cy - Yv, rt)
        else:
            span(s16(left), Yv + cy, s16(right)); span(s16(left), cy - Yv, s16(right))
    if w(rr * B) > 0:
        left = right = u16(cx); xs = 0
        while True:
            if d >= 0:
                emit(left, right, Y); Y -= 1; d = w(d - w(s16(Y) * w(4 * B)))
            d = w(d + w(w(2 * xs + 3) * w(2 * A))); left -= 1; X += 1; xs = s16(X); right += 1
            if not (w(s16(Y) * B) > w(xs * A)): break
    Xs, Ys = s16(X), s16(Y)
    d = w(w(w(w((Ys - 2) * Ys) + 1) * twoB) + w(w((Xs + 1) * Xs) * w(2 * A)) + w((1 - twoB) * A))
    if Ys + 1 == 0: return spans
    left, right = u16(cx - X), u16(cx + X)
    while True:
        emit(left, right, Y)
        if d <= 0: X += 1; d = w(d + w(s16(X) * w(4 * A))); left -= 1; right += 1
        Y -= 1; d = w(d + w((3 - 2 * s16(Y)) * twoB))
        if s16(Y) + 1 == 0: break
    return spans


def profile(spans, cx, cy, angles):
    out = {}
    for ang in angles:
        t = math.radians(ang); best = 0
        for k in range(1, 40000):
            x, y = cx + k * 0.25 * math.cos(t), cy - k * 0.25 * math.sin(t); sp = spans.get(int(round(y)))
            if sp and sp[0] <= x <= sp[1]: best = k * 0.25
            elif k * 0.25 > best + 3: break
        out[ang] = best
    return out


C = 3000
onset = next(r for r in range(1, 2000) if circlefill(C, C, r, 2 * C, 2 * C) != circlefill(C, C, r, 2 * C, 2 * C, wrap=False))
print(f'emulation: first radius (texels) whose spans differ from unbounded arithmetic: {onset}')
checks['onset_826'] = onset == 826


def max_abs_d(r):
    A = B = r * r; d = 2 * A * (1 - B) + 2 * B * r * (r - 1) + B; Y, X, m = r, 0, abs(d)
    while True:
        if d >= 0: Y -= 1; d -= Y * 4 * B; m = max(m, abs(d))
        d += (2 * X + 3) * 2 * A; X += 1; m = max(m, abs(d))
        if not (Y * B > X * A): break
    d = ((Y - 2) * Y + 1) * 2 * B + (X + 1) * X * 2 * A + (1 - 2 * B) * A; m = max(m, abs(d))
    while True:
        if d <= 0: X += 1; d += X * 4 * A; m = max(m, abs(d))
        Y -= 1; d += (3 - 2 * Y) * 2 * B; m = max(m, abs(d))
        if Y + 1 == 0: return m


first_wrap = next(r for r in range(1, 2000) if max_abs_d(r) >= 2 ** 31)
print(f'exact arithmetic: first radius whose decision variable reaches 2^31: {first_wrap} '
      f'(max |d| at {first_wrap - 1}: {max_abs_d(first_wrap - 1)})')
checks['no_wrap_up_to_813'] = first_wrap == 814
collapse = next(r for r in range(1200, 1400) if profile(circlefill(C, C, r, 2 * C, 2 * C), C, C, (25,))[25] < 0.6 * r)
print(f'emulation: first radius whose boundary at 25 deg falls below 0.6 r (region-1 exit product r^3 > 2^31): {collapse}; '
      f'r^3 at {collapse - 1}/{collapse}: {(collapse - 1) ** 3}/{collapse ** 3} vs 2^31 = {2 ** 31}')
checks['collapse_1291_at_r3_over_2p31'] = collapse == 1291 and (collapse - 1) ** 3 < 2 ** 31 < collapse ** 3
for r in (800, 825, 900, 1000, 1100, 1300, 1640):
    p = profile(circlefill(C, C, r, 2 * C, 2 * C), C, C, (0, 15, 25, 35, 45, 60, 75, 90))
    print('   r=%4d boundary/r at 0..90 deg:' % r, ' '.join('%d:%.3f' % (a, v / r) for a, v in p.items()))
meas = {0: 596, 25: 530, 45: 584, 60: 655}         # user's screenshot, real px from the ship (s = 1.5, d = 2)
fit = []
for r in range(813, 1700, 1):
    p = profile(circlefill(C, C, r, 2 * C, 2 * C), C, C, meas)
    fit.append((math.sqrt(sum((p[a] * 0.75 - m) ** 2 for a, m in meas.items())), r, {a: round(p[a] * 0.75) for a in meas}))
fit.sort()
print('fit to the user profile (texel radius, real px = texel * s/d = 0.75): best', fit[0][1], fit[0][2], 'rms %.1f' % (fit[0][0] / 2))
print('   next distinct radii:', [(f[1], round(f[0] / 2, 1)) for f in fit[1:6]])
checks['fit_near_1640'] = 1600 <= fit[0][1] <= 1680
sp = circlefill(512, 512, 1009, 1024, 1024)
full = sum(1 for y in range(1024) if sp.get(y) == (0, 1023))
print(f's = 1, d = 1 at the same zoom (r = 820 * 1024/832 = 1009 texels, centred, 1024 window): rows fully covered {full}/1024')
sp = circlefill(832, 832, 820, 1664, 1664)
p = profile(sp, 832, 832, range(0, 91, 5)); dev = max(abs(v / 820 - 1) for v in p.values())
print(f'50% zoom (r = 820 texels): max boundary deviation {dev:.4f}')

print('checks:', sum(checks.values()), '/', len(checks), 'pass')
for k, v in checks.items():
    if not v: print('  FAIL', k)
print('RESULT', 'PASS' if all(checks.values()) else 'FAIL')
