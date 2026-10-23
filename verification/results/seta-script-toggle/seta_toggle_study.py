#!/usr/bin/env python3
"""SETA on/off in the KC story object and X3AP.exe: every fact quoted in
docs/reverse-engineering/seta-script-toggle.md, re-derived read-only from the installed files.

Prints, per story build (Mayhem addon/07.cat, the copy the game loads; stock AP addon/04.cat for comparison):
identity, the class-150 SETA members and their accessors, decoded listings of the start/stop/toggle/ramp methods,
the SETA key case of 501::HandleKeyInput, every caller of StopFastForward/StartFastForward/ToggleFastForward and of
the TI_SetTimeWarpFactor native (with the return offset a task-stack walk sees), the gate-entry paths into
150::StartGateWarp, the MSCI "stop SETA" case. Then the text entries (key name, default factor, MSCI name), an
MSCI usage scan of addon/scripts, and the EXE byte checks (call shapes, hook-site bytes, branch-target scan).
Ends with RESULT PASS only if every check holds. Decoded instructions only; no script bytes are printed.

    python3 -I verification/results/seta-script-toggle/seta_toggle_study.py [X3 dir] > seta_toggle_study.txt
"""
import bisect, gzip, hashlib, re, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai/gate-queue'))
sys.path.insert(0, str(HERE.parents[2] / 'tools/analysis'))
from kc_obj import Obj, load_member          # noqa: E402
from inspect_x3 import read_catalogue        # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
FAIL = []


def check(ok, what):
    print('   [%s] %s' % ('ok' if ok else 'FAIL', what))
    if not ok: FAIL.append(what)


# ---------------------------------------------------------------- story object
def callers(o, entries=None, names=()):
    """(site, kind, target name, owner entry) for class/self calls to `entries` and native/by-name calls to `names`."""
    out = []
    for pc in o.all_insns():
        op = o.code[pc]
        if op in (0x84, 0x86, 0x88) and entries:
            v = struct.unpack_from('>I', o.code, pc + 1)[0]
            if v in entries: out.append((pc, 'call', entries[v]))
        elif op in (0x82, 0x85, 0x87) and o.operand(pc) in names:
            out.append((pc, {0x82: 'native', 0x85: 'byname', 0x87: 'byname87'}[op], o.operand(pc)))
    return out


def owner(o, pc):
    e = o.entries[bisect.bisect_right(o.entries, pc) - 1]
    return e, '%d::%s' % o.meth[e][0][:2]


def switch_cases(o, pc):
    """op 0x78 (handler 0x004a37bc): u16 n, BE32 base, BE32 default, n BE32 targets for base..base+n-1."""
    n = struct.unpack_from('>H', o.code, pc + 1)[0]
    w = struct.unpack_from('>%di' % (n + 2), o.code, pc + 3)
    return {w[0] + i: t for i, t in enumerate(w[2:]) if t != w[1]}


def study(label, cat):
    raw = load_member(cat); o = Obj(raw)
    # functions without a CLAS row (op 0x84 targets, e.g. Mayhem's save-description helper 0bd7f): pseudo entries,
    # so that owner() does not attribute their code to the preceding method
    for pc in o.all_insns():
        if o.code[pc] == 0x84:
            v = struct.unpack_from('>I', o.code, pc + 1)[0]
            if v not in o.meth: o.meth[v] = [(0, 'fn_%05x' % v, -1)]
    o.entries = sorted(o.meth)
    print('=' * 110)
    print('%s: %s:L/x3story.obj  %d B  SHA-256 %s  build %d' % (label, cat.relative_to(X3), len(raw),
                                                                  hashlib.sha256(raw).hexdigest(), o.build))
    meth = {n: o.find(150, n) for n in ('StartFastForward', 'StopFastForward', 'ToggleFastForward', 'IsFastForward',
                                          'GetWarpFactor', 'SetWarpFactor', 'GetWarpMultiplier', 'Vbi', 'Input',
                                          'InputAction', 'StartGateWarp', 'StartSectorWarp', 'QuickWarp',
                                          'WarpToSector', 'IsWarping', 'NotifyCollisionWarn')}
    print('  class 150 entries: ' + ', '.join('%s %05x' % (k, v[0]) for k, v in meth.items() if v))
    R = {'label': label, 'o': o}
    # members: every member StartFastForward/StopFastForward/GetWarpMultiplier touch, plus StartGateWarp's guard
    mem_of = lambda e, ops: {struct.unpack_from('>h', o.code, pc + 1)[0] for pc in o.insns(e) if o.code[pc] in ops}
    seta_m = set()
    for n in ('StartFastForward', 'StopFastForward', 'GetWarpMultiplier'): seta_m |= mem_of(meth[n][0], (0x0f, 0x16))
    seta_m.add(min(mem_of(meth['StartGateWarp'][0], (0x16,))))
    acc = {}
    for e, ms in o.meth.items():
        for c, n, a in ms:
            if c != 150: continue
            for pc in o.insns(e):
                if o.code[pc] in (0x0f, 0x16):
                    v = struct.unpack_from('>h', o.code, pc + 1)[0]
                    if v in seta_m:
                        acc.setdefault(v, {'ld': set(), 'st': set()})['ld' if o.code[pc] == 0x0f else 'st'].add(n)
    print('  class-150 member accessors (writers | readers):')
    for v in sorted(acc):
        print('    m%-3d st: %-60s ld: %s' % (v, ', '.join(sorted(acc[v]['st'])), ', '.join(sorted(acc[v]['ld']))))
    R['acc'] = acc
    if label.startswith('Mayhem'):
        for n in ('StartFastForward', 'StopFastForward', 'ToggleFastForward', 'GetWarpMultiplier', 'IsFastForward',
                  'SetWarpFactor', 'InputAction'):
            e = meth[n][0]; print('\n  -- 150::%s %05x' % (n, e))
            for pc in o.insns(e): print('     ' + o.line(pc))
        e = meth['Vbi'][0]; print('\n  -- 150::Vbi %05x, SETA ramp window' % e)
        for pc in o.insns(e):
            if 0x142df <= pc < 0x14367: print('     ' + o.line(pc))
        for n in ('IncreaseTimeWarp', 'DecreaseTimeWarp'):
            e = o.find(203, n)[0]; print('\n  -- 203::%s %05x' % (n, e))
            for pc in o.insns(e): print('     ' + o.line(pc))
    # HandleKeyInput switch
    hk = o.find(501, 'HandleKeyInput')[0]
    sw = [pc for pc in o.insns(hk) if o.code[pc] == 0x78]
    cases = switch_cases(o, sw[0])
    tog, stp = meth['ToggleFastForward'][0], meth['StopFastForward'][0]
    def case_calls(v):
        t = cases.get(v); out = []
        if t is None: return out
        pc = t
        while o.code[pc] != 0x83:
            if o.code[pc] in (0x86, 0x88):
                x = struct.unpack_from('>I', o.code, pc + 1)[0]; out.append('%d::%s' % o.meth[x][0][:2])
            pc += o.width(pc)
        return out
    print('\n  501::HandleKeyInput %05x: switch at %05x, %d cases; action -> calls:' % (hk, sw[0], len(cases)))
    for v in (675, 825, 971, 972):
        print('    action %d -> %05x %s' % (v, cases.get(v, 0), case_calls(v)))
    R['case675'] = case_calls(675)
    R['actions_toggle'] = sorted(v for v in cases if 'ToggleFastForward' in ' '.join(case_calls(v)))
    # callers
    ents = {meth[n][0]: n for n in ('StopFastForward', 'StartFastForward', 'ToggleFastForward', 'IsFastForward',
                                    'StartGateWarp', 'StartSectorWarp', 'QuickWarp', 'SetWarpFactor', 'GetWarpMultiplier')}
    rows = callers(o, ents, ('TI_SetTimeWarpFactor', 'INS_SetTimeWarpFactor', 'StopFastForward', 'StartFastForward',
                             'ToggleFastForward', 'CanWarp'))
    print('\n  callers (site, return offset = site + 5, kind, target, owner):')
    R['callers'] = {}
    for pc, kind, nm in rows:
        e, on = owner(o, pc)
        print('    %05x ret %05x  %-7s %-22s in %s (%05x)' % (pc, pc + 5, kind, nm, on, e))
        R['callers'].setdefault(nm, []).append((pc, on))
    # gate entry: who calls StartGateWarp, and the player guard in CanWarp
    cw = o.find(2004, 'CanWarp')[0]
    lines, _ = o.track(cw)
    g = [l for _, _, l in lines if 'StartGateWarp' in l or 'IsWarping' in l or '524288' in l or 'g10' in l]
    print('\n  2004::CanWarp %05x (engine gate-collision event, EXE 0x0045d7aa): ' % cw + ' | '.join(g))
    R['canwarp'] = g
    # MSCI case
    rs = o.find(702, '__runScript')[0]
    tg = {}
    for pc in o.insns(rs):
        if o.code[pc] == 0x78:
            for v, t in switch_cases(o, pc).items(): tg.setdefault(t, []).append(v)
    ks = sorted(tg); R['msci'] = []
    for pc, on in R['callers'].get('StopFastForward', []):
        if on == '702::__runScript':
            t = ks[bisect.bisect_right(ks, pc) - 1]
            print('  702::__runScript StopFastForward at %05x: case target %05x, MSCI instruction %s' % (pc, t, tg[t]))
            R['msci'] += tg[t]
    # call trees of the start/stop methods: blocking natives or dynamic calls (depth <= 4)
    BLOCK = {'TI_Delay', 'TI_DelayRandom', 'TI_WaitForInterrupt', 'TI_WaitRandomForInterrupt', 'SA_Idle'}
    def tree(e, depth, seen, bad):
        if e in seen or depth > 4: return
        seen.add(e)
        for pc in o.insns(e):
            op = o.code[pc]
            if (op == 0x82 and o.operand(pc) in BLOCK) or op in (0x85, 0x87): bad.append('%05x %s' % (pc, o.operand(pc)))
            if op in (0x84, 0x86, 0x88):
                v = struct.unpack_from('>I', o.code, pc + 1)[0]
                if v in o.meth: tree(v, depth + 1, seen, bad)
    R['trees'] = {}
    for n in ('StartFastForward', 'StopFastForward'):
        seen, bad = set(), []; tree(meth[n][0], 0, seen, bad)
        R['trees'][n] = bad
        print('  150::%s reaches %s; blocking natives or dynamic calls: %s' % (
            n, sorted('%d::%s' % o.meth[x][0][:2] for x in seen), bad or 'none'))
    # option (iii): StartGateWarp's StopFastForward call retargeted to IsFastForward (argc 0, pure: ldmem; ret)
    sg = meth['StartGateWarp'][0]
    site = [pc for pc in o.insns(sg) if o.code[pc] == 0x86 and struct.unpack_from('>I', o.code, pc + 1)[0] == stp]
    isf = meth['IsFastForward'][0]
    body = [(o.code[pc], o.operand(pc)) for pc in o.insns(isf)]
    pat = bytearray(raw); fo = o.code_file_off + site[0] + 1
    pat[fo:fo + 4] = struct.pack('>I', isf)
    P = Obj(bytes(pat))
    starts_equal = list(o.all_insns()) == list(P.all_insns())
    ndiff = sum(1 for a, b in zip(raw, pat) if a != b)
    print('\n  option (iii): 150::StartGateWarp callcls at %05x (file +0x%x, operand file +0x%x..+0x%x): '
          'StopFastForward %05x -> IsFastForward %05x; %d bytes differ; length %s; instruction starts equal %s; '
          'IsFastForward body %s; argc %s; build id kept %s' % (
              site[0], o.code_file_off + site[0], fo, fo + 3, stp, isf, ndiff, len(pat) == len(raw), starts_equal,
              [MN for MN, _ in body][:3], [m[2] for m in o.meth[isf] if m[0] == 150], P.build == o.build))
    R['patch_ok'] = (len(site) == 1 and ndiff <= 4 and len(pat) == len(raw) and starts_equal and P.build == o.build
                     and [b[0] for b in body][:3] == [0x6e, 0x0f, 0x83])
    return R


M = study('Mayhem (installed)', X3 / 'addon/07.cat')
S = study('stock AP 3.3', X3 / 'addon/04.cat')

# ---------------------------------------------------------------- comparison and story checks
print('=' * 110)
print('story checks')
o = M['o']
own = lambda R, nm: sorted({on for _, on in R['callers'].get(nm, [])})
print('  StopFastForward owners, Mayhem only: %s' % sorted(set(own(M, 'StopFastForward')) - set(own(S, 'StopFastForward'))))
print('  StopFastForward owners, stock only:  %s' % sorted(set(own(S, 'StopFastForward')) - set(own(M, 'StopFastForward'))))
print('  TI_SetTimeWarpFactor owners: Mayhem %s; stock %s' % (own(M, 'TI_SetTimeWarpFactor'), own(S, 'TI_SetTimeWarpFactor')))
check(own(M, 'StartFastForward') == ['150::ToggleFastForward'], 'StartFastForward has one caller, ToggleFastForward')
check(own(M, 'ToggleFastForward') == ['501::HandleKeyInput', '605::ClickDisplayPanel'],
      'ToggleFastForward callers: HandleKeyInput (key action) and ClickDisplayPanel')
check(M['case675'] == ['150::ToggleFastForward'] and M['actions_toggle'] == [675], 'action 675 is the only toggle case')
check(M['acc'][21]['st'] == {'StartFastForward', 'StopFastForward'}, 'm21 written only by Start/StopFastForward')
check(M['acc'][22]['st'] == {'StartFastForward', 'StopFastForward', 'Vbi'}, 'm22 written by Start/Stop and the Vbi ramp')
check(M['acc'][24]['st'] == {'InitClient', 'SetWarpFactor'}, 'm24 (target factor) written by InitClient and SetWarpFactor')
check(M['acc'][9]['st'] == {'SetKilled', 'StartKilled'}, 'm9 written only by SetKilled/StartKilled')
check(('150::StartGateWarp' in own(M, 'StopFastForward')) and (0x15ef3 in [p for p, _ in M['callers']['StopFastForward']]),
      'StartGateWarp calls StopFastForward at 15ef3 (return offset 15ef8)')
check(any('StartGateWarp' in l for l in M['canwarp']), 'CanWarp reaches StartGateWarp (manual gate entry)')
check(sorted(M['msci']) == [1335, 1927], 'the __runScript StopFastForward cases are MSCI 1927 (stop SETA) and 1335 (move player to ship)')
check('150::NotifyCollisionWarn' not in own(M, 'StopFastForward'), 'Mayhem NotifyCollisionWarn does not stop SETA')
check(M['patch_ok'] and S['patch_ok'], 'option (iii) retarget: one site, <= 4 bytes, length/build/instruction starts kept, '
      'IsFastForward = frame; ldmem; ret (both builds)')
check(M['trees']['StartFastForward'] == [] and M['trees']['StopFastForward'] == [],
      'Start/StopFastForward reach no blocking native and no dynamic call (run to completion in one call)')
check('0::fn_0bd7f' in own(M, 'StopFastForward'), 'the save-description helper 0bd7f (no CLAS row) stops SETA')

# ---------------------------------------------------------------- text resources
print('=' * 110)
print('text and MSCI scripts')
def pck(cat, path):
    for e in read_catalogue(cat):
        if e['path'] == path:
            with open(cat.with_suffix('.dat'), 'rb') as f:
                f.seek(e['offset']); return gzip.decompress(bytes(b ^ 0x33 for b in f.read(e['size']))).decode('utf-8', 'replace')
def tid(t, page, i):
    a = t.find('<page id="%s"' % page)
    if a < 0: return None
    m = re.search(r'<t id="%d">([^<]*)</t>' % i, t[a:t.find('</page>', a)]); return m.group(1) if m else None
t = pck(X3 / 'addon/04.cat', 'addon/t/0001-L044.pck')
for i in (10675, 10825): print('  addon/04.cat 0001-L044 page 301903 id %d: %r' % (i, tid(t, '301903', i)))
check(tid(t, '301903', 10675) == 'SETA time accelerator' and tid(t, '301903', 10825) == 'Pause game',
      'control names 10000+action: 675 = SETA, 825 = pause (matches the switch)')
m = (X3 / 'addon/t/8383-L044.xml').read_text('utf-8', 'replace')
d68, d1927, d1335 = tid(m, '68', 200), None, tid(t, '352003', 1335)
a = m.find('id="1927"'); d1927 = re.search(r'id="1927">([^<]*)<', m[a - 1:]).group(1) if a >= 0 else None
print('  addon/t/8383-L044.xml page 68 id 200 (m24 default via SE_ReadText): %r; id 1927: %r; AP 352003/1335: %r'
      % (d68, d1927, d1335))
check(d68 == '10', 'default SETA factor m24 = 10')
n = hits = 0
for p in sorted((X3 / 'addon/scripts').glob('*.xml')):
    s = p.read_text('latin1'); n += 1
    if re.search(r'<sval type="int" val="1927"\s*/>', s[max(0, s.find('<codearray>')):]): hits += 1
print('  addon/scripts: %d XML scripts scanned, %d contain an int 1927 in their code array' % (n, hits))
check(hits == 0, 'no installed MSCI script uses "stop SETA" (1927)')

# ---------------------------------------------------------------- EXE
print('=' * 110)
EXE = X3 / 'X3AP.exe'
d = EXE.read_bytes()
sha = hashlib.sha256(d).hexdigest(); print('X3AP.exe SHA-256 %s' % sha)
check(sha.startswith('fdbf3418d8f0a897'), 'EXE identity')
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec, opt = struct.unpack_from('<H', d, pe + 6)[0], struct.unpack_from('<H', d, pe + 20)[0]
secs = [struct.unpack_from('<IIII', d, pe + 24 + opt + 40 * i + 8) for i in range(nsec)]
def off(va):
    r = va - 0x400000
    for vs, sva, rs, ra in secs:
        if sva <= r < sva + max(vs, rs): return ra + r - sva
    raise ValueError(hex(va))
rd = lambda va, n: d[off(va):off(va) + n]
cstr = lambda va: d[off(va):d.index(b'\0', off(va))].decode('latin1')
BYTES = [
    (0x0041210d, '8b 15 34 6f 60 00 81 ba cc 00 00 00 00 00 01 00 74 17',
     'input routine: if cfg+0xcc != 0x10000 (after one of nine input flags) ...'),
    (0x0041211f, '53 6a 01 68 1c 5e 55 00 68 96 00 00 00 53 53 e8 8d d3 08 00 83 c4 18',
     '... 0x0049f4c0(0, 0, 0x96, "StopFastForward", 1, 0)'),
    (0x00410b89, '33 c0 a3 60 8e 60 00 a3 88 8e 60 00 a3 b0 8e 60 00', 'the input flags are cleared at 0x00410b8b..'),
    (0x0040f99f, '50 6a 01 6a 01 68 00 5e 55 00 68 96 00 00 00 6a 00 6a 00 e8 09 fb 08 00 83 c4 1c',
     'action source: 0x0049f4c0(0, 0, 0x96, "InputAction", 1, 1, action)'),
    (0x0045d79e, '68 64 fd 55 00', 'gate collision: push "CanWarp" before 0x0049f4c0 at 0x0045d7aa'),
    (0x0045d7aa, 'e8 11 1d 04 00', '... call 0x0049f4c0'),
    (0x004b148c, 'a1 34 6f 60 00 8b 53 01', 'TI case 9 TI_SetTimeWarpFactor entry: mov eax,[cfg]; mov edx,[ebx+1] (8 B)'),
    (0x004b1494, '8b 0d e4 85 60 00 6a 01 89 90 cc 00 00 00', '... mov ecx,[VM]; push 1; mov [eax+0xcc],edx'),
    (0x004b14a2, '51 8b c5 e8 46 33 ff ff b8 01 00 00 00', '... push ecx; mov eax,ebp(task); call 0x004a47f0; eax=1'),
    (0x00410100, '55 8b ec 83 e4 f0', 'input routine entry (the chase-restore inject site)'),
    (0x0049f4c0, '83 ec 0c 53 55 56 57', 'class/by-key wrapper 0x0049f4c0 prologue'),
    (0x004a26dc, '83 c0 ff', 'interpreter: table index = opcode - 1'),
    (0x004a37bc, '0f b7 37 8b 57 02', 'op 0x78 handler: u16 n, base ...'),
]
print('EXE byte checks')
for va, hx, what in BYTES:
    check(rd(va, len(bytes.fromhex(hx))) == bytes.fromhex(hx), '%08x %s' % (va, what))
for va, s in ((0x00555e1c, 'StopFastForward'), (0x00555e00, 'InputAction'), (0x0055fd64, 'CanWarp')):
    check(cstr(va) == s, '%08x = "%s"' % (va, s))
check(struct.unpack('<I', rd(0x004b15f8 + 9 * 4, 4))[0] == 0x004b148c, 'TI jump table 0x004b15f8[9] = 0x004b148c')
text = [(sva + 0x400000, ra, max(vs, rs)) for vs, sva, rs, ra in secs if sva == 0x1000][0]
tv, tr, tn = text; T = d[tr:tr + tn]
for name in (b'StopFastForward\0', b'StartFastForward\0', b'ToggleFastForward\0', b'IsFastForward\0'):
    print('   EXE string %-20s occurrences %d' % (name[:-1].decode(), d.count(b'\0' + name)))
check(d.count(b'\0StartFastForward\0') == 0, 'the EXE carries no "StartFastForward" literal (a caller must supply it)')
check(T.count(b'\x68\x1c\x5e\x55\x00') == 1, 'one push of the "StopFastForward" pointer in .text')
# conservative branch-target scan into the interior of the TI case-9 span [0x004b148d, 0x004b14b3)
lo, hi, cand = 0x004b148d, 0x004b14b3, []   # the whole case-9 body after its first byte
for i in range(len(T) - 6):
    va = tv + i; b0 = T[i]
    if b0 in (0xe8, 0xe9): tgt = va + 5 + struct.unpack_from('<i', T, i + 1)[0]
    elif b0 == 0x0f and 0x80 <= T[i + 1] <= 0x8f: tgt = va + 6 + struct.unpack_from('<i', T, i + 2)[0]
    elif b0 == 0xeb or 0x70 <= b0 <= 0x7f: tgt = va + 2 + struct.unpack_from('<b', T, i + 1)[0]
    else: continue
    if lo <= tgt < hi: cand.append((va, tgt))
dw = [tv + i for i in range(0, len(T) - 3) if lo <= struct.unpack_from('<I', T, i)[0] < hi]
print('   byte-pattern branch candidates into %08x..%08x: %s; dword references: %s' % (
    lo, hi, ['%08x->%08x' % c for c in cand][:12], ['%08x' % x for x in dw][:12]))
print('   (candidates are any byte offset decoding as a branch; 004b147c is the rel32 operand of the call at 004b147b,'
      ' and 004b14b1 lies outside the 8-byte span)')
check(not [c for c in cand if 0x004b148d <= c[1] < 0x004b1494] and not [x for x in dw if True],
      'no branch or dword reference into the interior of the 8-byte span 0x004b148c..0x004b1493')
print('\nRESULT %s (%d failures)' % ('PASS' if not FAIL else 'FAIL', len(FAIL)))
for f in FAIL: print('  FAIL: ' + f)
