#!/usr/bin/env python3
"""The SETA key end to end: how a held keyboard key reaches 150::ToggleFastForward, and why one press can toggle more
than once. Every EXE and KC fact quoted in docs/reverse-engineering/seta-manual-flight.md section 7, re-derived
read-only from the installed files, plus a model of the key ring and the script's repeat filter.

EXE: the DirectInput keyboard poll 0x004d5a90 (every held key pushed into the ring each poll; write index period 3),
the ring reader 0x004d3b60 (read index period 4; DIK byte of slot 3 never written), DIK 0 -> engine code 0, the one
ring read per main-loop iteration 0x00404280 (Input(key), also for an empty read unless [0x00607c60]), the action
counters 0x0057fc68 (cleared at the 0x00410100 entry, incremented per held translated key at 0x00410a6a), the two
EXE InputAction sites. KC (Mayhem addon/07.cat; stock addon/04.cat for the filter): 500::InputFrameMap and the
501::PerFrameInput repeat filter, the action-675 dispatch, every caller of StartFastForward/ToggleFastForward, the
ramp guard. Optional: the control profile (SETA key binding, steering keys, no tap/hold qualifier on the SETA key).
Then the ring + filter model: SETA toggles per press, alone and with steering keys held.

Ends with RESULT PASS only if every check holds. Prints derived facts, short instruction bytes and decoded KC
instructions only.

    python3 -I verification/results/seta-key-toggle/seta_key_toggle_study.py [X3 dir] [profile.xpf] > seta_key_toggle_study.txt
"""
import collections, hashlib, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai/gate-queue'))
from kc_obj import Obj, load_member          # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
PROFILE = Path(sys.argv[2]) if len(sys.argv) > 2 else None
FAIL = []


def check(ok, what):
    print('   [%s] %s' % ('ok' if ok else 'FAIL', what))
    if not ok: FAIL.append(what)


# ---------------------------------------------------------------- EXE
d = (X3 / 'X3AP.exe').read_bytes()
sha = hashlib.sha256(d).hexdigest()
print('X3AP.exe SHA-256 %s' % sha)
check(sha.startswith('fdbf3418d8f0a897'), 'EXE identity')
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]


def sec_of(va):
    r = va - 0x400000
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs): return vs, sva, rs, ra
    raise ValueError(hex(va))


def rd(va, n):
    vs, sva, rs, ra = sec_of(va); r = va - 0x400000
    assert r + n <= sva + rs, 'read beyond raw data at %x' % va
    return d[ra + r - sva:ra + r - sva + n]


def bss(va):
    vs, sva, rs, ra = sec_of(va); return va - 0x400000 >= sva + rs


def same(va, hexbytes, what):
    b = bytes.fromhex(hexbytes)
    check(rd(va, len(b)) == b, '%s: %08x  %s' % (what, va, hexbytes))


print('\n1. DirectInput keyboard poll 0x004d5a90 -> key ring')
same(0x004d5b97, 'b9 0a 00 00 00 3b 8a 28 07 00 00', 'buffer count: 10 vs cfg+0x728 (1 event per poll above, else 4)')
same(0x004d5cc4, '81 f9 ff 00 00 00 73 0a 8a 50 04 88 94 31 10 02 00 00',
     'each buffered event updates the persistent DIK state ctx+0x210')
same(0x004d5db0, 'f6 84 37 10 02 00 00 80 74 65', 'loop over DIK 0..255: test state & 0x80 (held), else skip')
same(0x004d5ddc, 'c6 44 30 10 01', 'held key -> ctx+0x10[code] = 1 (down table, cleared at poll start)')
same(0x004d5dd2, '66 83 fb 36', 'skip RSHIFT'); same(0x004d5de3, '66 83 fb 2a', 'skip LSHIFT')
same(0x004d5de9, '66 a1 7c 84 57 00 66 05 01 00 66 3d 03 00',
     'push: wi = wi + 1; compare the NEW index with 3 (period 3: 1, 2, 0)')
same(0x004d5e09, '66 33 c0 66 a3 7c 84 57 00', 'push: wi >= 3 -> 0')
same(0x004d5e18, '88 1c 95 e8 5e 59 00', 'push: DIK byte -> slot[wi] (0x00595ee8 + 12*wi); no read-index test')
same(0x004d5e25, '66 81 fb 00 01 7c 84', 'every held DIK is pushed on every poll (no edge test)')

print('\n2. Ring reader 0x004d3b60 (one entry per call)')
same(0x004d3b8c, '66 a1 88 84 57 00 66 8b 0d 7c 84 57 00 66 3b c1 0f 84 1e 01 00 00', 'empty when ri == wi -> 0')
same(0x004d3ba2, '66 8b d0 66 05 01 00 66 83 fa 03 66 a3 88 84 57 00 7c 09 66 33 c0',
     'ri = ri + 1; reset to 0 only when the OLD index was >= 3 (period 4: 1, 2, 3, 0)')
same(0x004d3be7, '0f bf c0 8d 04 40 0f b6 04 85 e8 5e 59 00 e8 56 2c 00 00',
     'DirectInput mode: DIK byte of slot[ri] -> 0x004d6850')
same(0x004d3a88, '66 a1 7c 84 57 00 66 8b c8 66 03 c2 66 83 f9 03',
     'the WM writer (only without a DirectInput keyboard) uses the period-4 rule of the reader')
slot3 = 0x00595ee8 + 36
check(bss(slot3), 'slot 3 DIK byte 0x%08x lies in zero-initialised .bss' % slot3)
TEXT = [s for s in secs if s[1] == 0x1000][0]
text = d[TEXT[3]:TEXT[3] + TEXT[2]]
check(struct.pack('<I', slot3) not in d, 'no absolute reference to 0x%08x anywhere in the file' % slot3)
check(text.count(b'\xe8\x5e\x59\x00') == 5, 'five .text uses of the slot base 0x00595ee8 (one writer, four readers)')
same(0x004d6850, '83 c0 ff 3d ec 00 00 00 0f 87 6e 03 00 00', 'DIK 0 -> above the table')
same(0x004d6bcc, '33 c0 c3', '... -> engine code 0 (| 0x1000 with Shift)')
same(0x004d57d1, 'a1 74 8b 60 00 8b 08 8b 51 0c 57 68 7c 8b 60 00', 'the keyboard device is always created')
same(0x004d37a3, '8b 0d 74 8b 60 00 85 c9 74 09 83 3d 7c 8b 60 00 00 75 1f',
     'window procedure: WM_KEYDOWN/WM_CHAR feed the ring only without a DirectInput keyboard')

print('\n3. One ring read and one script Input call per main-loop iteration')
same(0x00403af5, 'e8 b6 f9 0c 00', 'main loop: pump (DirectInput poll) per iteration')
same(0x00403b3a, '39 ae d8 04 00 00 0f 85 72 02 00 00', 'game mode -> 0x00403db8')
same(0x00403db8, 'e8 c3 04 00 00', '0x00403db8 call 0x00404280')
same(0x0040428c, 'e8 cf f8 0c 00', '0x00404280 reads ONE ring entry')
same(0x004042a2, '66 89 35 3c 90 60 00', 'last read key -> 0x0060903c')
same(0x004042d1, '83 3d 60 7c 60 00 00 74 05 66 85 f6 74 1a 0f bf ce e8 89 e3 ff ff',
     'Input(key) runs for every read, an empty read (0) included, unless [0x00607c60] (an Input from 0x00410100 '
     'in the previous iteration)')
same(0x00402681, '68 7c 53 55 00 68 96 00 00 00', '0x00402670 = by-name "Input" on class 0x96')
check(rd(0x0055537c, 6) == b'Input\0', 'string 0x0055537c "Input"')
same(0x00404363, 'e8 98 bd 00 00', '0x00410100 runs after the read, in the same iteration')

print('\n4. 0x00410100 keyboard loop and the action counters')
same(0x00410130, '68 00 04 00 00 6a 00 68 68 fc 57 00 e8 9f 8c 10 00',
     'entry: memset(0x0057fc68, 0, 0x400) at 0x0041013c (the 0x00410100 inject stub runs before it)')
same(0x004109f0, '80 7c 3e 10 00 0f 84 a1 00 00 00', 'per engine code 0..0x1ff: down this poll?')
check(rd(0x0055539c, 13) == b'TranslateKey\0', 'held key -> by-name TranslateKey (0x0055539c)')
same(0x00410a5a, '85 c9 74 18 3b cf 74 14 81 f9 00 04 00 00 7d 0c 80 81 68 fc 57 00 01',
     'translated (1..0x3ff, != key) -> counter[action] += 1 at 0x00410a6a; else press time, Input(k|0x2000/0x4000) '
     'on release')
print('   counter of action 675 (SETA): u8 at 0x%08x' % (0x0057fc68 + 675))
ia = [i for i in range(len(text) - 5) if text[i] == 0x68 and text[i + 1:i + 5] == struct.pack('<I', 0x00555e00)]
check(len(ia) == 2, 'two EXE pushes of "InputAction" (0x00555e00): look 0x2cd..0x2d4 and mouse steering 0x230/0x238')

# ---------------------------------------------------------------- KC
print('\n5. Story script (Mayhem addon/07.cat)')
o = Obj(load_member(X3 / 'addon/07.cat'))
print('   build id %d' % o.build)
check(o.build == 1462141839, 'Mayhem build id')
ent = lambda cid, nm: o.find(cid, nm)[0]


def track_text(ob, e):
    return [ln for pc, dep, ln in ob.track(e)[0]]


ifm = track_text(o, ent(500, 'InputFrameMap'))
check('m15 = m14' in ifm and 'm14 = 500::TranslateVKey(m13)' in ifm and 'p0 = m14' in ifm,
      '500::InputFrameMap: m15 = previous action, m14 = TranslateVKey(key), returns m14')
check(track_text(o, ent(500, 'GetLastInputFrameKey')) == ['frame 1', 'return p0'] and
      o.line(ent(500, 'GetLastInputFrameKey') + 3).split()[3:5] == ['ldmem(0f)', '15'],
      '500::GetLastInputFrameKey returns m15 (the action of the previous Input call)')
tvk = ent(500, 'TranslateVKey')
check([o.line(p).split()[3] for p in list(o.insns(tvk))[1:6]] == ['ldloc(0d)', 'not(64)', 'jf(34)', 'push0(01)', 'ret(83)'],
      '500::TranslateVKey(0) returns 0')
pfi = ent(501, 'PerFrameInput')
seq = [(p, o.line(p).split()[3], ' '.join(o.line(p).split()[4:])) for p in o.insns(pfi) if 0x1c625 <= p < 0x1c674]
want = [('ldloc(0d)', '3'), ('ldloc(0d)', '10'), ('eq(5b)', ''), ('jt(33)', '1c643'), ('ldloc(0d)', '1'),
        ('ldloc(0d)', '3'), ('ne(5a)', ''), ('jt(33)', '1c643')]
check([(m, a) for _, m, a in seq[:8]] == want,
      '501::PerFrameInput 1c625: last action == action, or context changed -> return 0 (repeat filter)')
n = struct.unpack_from('>H', o.code, 0x1c674 + 1)[0]
w = struct.unpack_from('>%di' % (n + 2), o.code, 0x1c674 + 3)
check(w[0] <= 675 < w[0] + n and w[2 + 675 - w[0]] == 0x1c65a and w[1] == 0x1cd57,
      'switch 1c674 (base %d, %d slots): action 675 -> 1c65a -> 501::HandleKeyInput (behind the filter)' % (w[0], n))
check(o.operand(0x1c661) == '501::HandleKeyInput' and o.operand(0x1d349) == '150::ToggleFastForward',
      '1c661 HandleKeyInput; case 675 at 1d345 -> 150::ToggleFastForward (no debounce in either)')
callsto = collections.defaultdict(list)
for pc in o.all_insns():
    if o.code[pc] in (0x84, 0x86, 0x88):
        v = struct.unpack_from('>I', o.code, pc + 1)[0]
        if v in o.meth: callsto['%d::%s' % o.meth[v][0][:2]].append(pc)
import bisect
owner = lambda pc: '%d::%s' % o.meth[o.entries[bisect.bisect_right(o.entries, pc) - 1]][0][:2]
starts = sorted(owner(p) for p in callsto['150::StartFastForward'])
toggles = sorted(owner(p) for p in callsto['150::ToggleFastForward'])
print('   StartFastForward callers %s; ToggleFastForward callers %s' % (starts, toggles))
check(starts == ['150::ToggleFastForward'], 'StartFastForward: only caller ToggleFastForward')
check(toggles == ['501::HandleKeyInput', '605::ClickDisplayPanel'], 'ToggleFastForward: key action 675, display click')
check(not callsto['150::InputAction'], 'no script call of 150::InputAction (the engine is its only caller)')
vbi = track_text(o, ent(150, 'Vbi')); stop = track_text(o, ent(150, 'StopFastForward'))
check('if not (m21) goto 1435a' in vbi and 'if not ((TI_CmpTime(TI_GetRealTime(), m23) > 250)) goto 14313' in vbi,
      '150::Vbi ramp step only while m21, > 250 ms real time apart (synchronous, inside 150::Input)')
check('m21 = 0' in stop and 'm22 = 0' in stop and 'call TI_SetTimeWarpFactor(65536)' in stop,
      'StopFastForward: m21 = 0, m22 = 0, factor 1.0 (the ramp cannot continue)')
s = Obj(load_member(X3 / 'addon/04.cat'))
sifm = track_text(s, s.find(500, 'InputFrameMap')[0])
check([l for l in sifm if not l.startswith(('goto', 'if', 'let'))] == [l for l in ifm if not l.startswith(('goto', 'if', 'let'))],
      'stock AP (build %d) InputFrameMap: same statements (vanilla filter)' % s.build)

# ---------------------------------------------------------------- profile
if PROFILE and PROFILE.exists():
    print('\n6. Control profile %s' % PROFILE.name)
    rows = [l.split(';') for l in PROFILE.read_text('latin1').splitlines()[1:] if l.count(';') == 2]
    keys = [(int(a), int(k)) for t, a, k in rows if t == '0' and a.isdigit() and k.isdigit()]
    seta = [k for a, k in keys if a == 675]
    print('   SETA (675) on %s; yaw/pitch 524..527 on %s; roll 528/529 on %s' % (
        [hex(k) for k in seta], [hex(k) for a, k in keys if 524 <= a <= 527], [hex(k) for a, k in keys if a in (528, 529)]))
    check(seta == [0x6a], 'SETA on engine code 0x6a (DIK_J), plain')
    check(not any(k & 0x6000 and k & 0xfff == 0x6a for a, k in keys), 'no tap/hold-qualified binding on 0x6a')

# ---------------------------------------------------------------- model
print('\n7. Model: SETA toggles per press (one poll and one read per iteration; exact index rules above; the script '
      'dispatches 675 when the previous Input call mapped to another action)')
J, L, U = 0x24, 0xCB, 0xC8


def press(n_hold, steer, ri, wi, reader_period=4, pre=3, post=8):
    slot = [0, 0, 0, 0]; prev = None; t = 0
    for f in range(pre + n_hold + post):
        held = set(steer) | ({J} if pre <= f < pre + n_hold else set())
        for k in sorted(held):
            wi += 1
            if wi >= 3: wi = 0
            slot[wi] = k
        if ri == wi: key = 0
        else:
            old = ri; ri = old + 1
            if old >= reader_period - 1: ri = 0
            key = slot[ri]
        if key == J and prev != J: t += 1
        prev = key
    return t


for rp, tag in ((4, 'engine (reader period 4)'), (3, 'hypothetical consistent ring (period 3)')):
    for steer, name in (((), 'J alone'), ((L,), 'J + one arrow held'), ((L, U), 'J + two arrows held')):
        cells = []
        for nh in (2, 4, 6, 10, 16, 22, 30):
            c = collections.Counter(press(nh, steer, ri, wi, rp) for ri in range(rp) for wi in range(3))
            tot = sum(c.values()); ev = sum(v for k, v in c.items() if k % 2 == 0)
            cells.append('%2d:%4.1f/%3d%%' % (nh, sum(k * v for k, v in c.items()) / tot, round(100 * ev / tot)))
        print('   %-40s %-20s %s' % (tag, name, '  '.join(cells)))
print('   (frames held : mean toggles / share of start phases with an even count = the press undoes itself)')
check(press(1, (), 0, 0) == 1, 'model sanity: a one-frame press toggles once')

print('\nRESULT %s (%d failures)' % ('PASS' if not FAIL else 'FAIL', len(FAIL)))
for f in FAIL: print('  FAIL: %s' % f)
