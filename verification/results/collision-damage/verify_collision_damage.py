#!/usr/bin/env python3
"""Read-only verifier of the collision-damage sites in X3AP.exe and X3FL.exe
(docs/reverse-engineering/collision-damage.md).

Per edition, from the clean file bytes at the preferred base 0x400000:
  * the executable SHA-256 and the SHA-256 of the whole collision response routine;
  * every byte window the note quotes (patch sites, their context, the damage calls, the class gate of the
    contact branch, the planet-atmosphere law), compared exactly;
  * the rel32 call census of the damage routine (AP 0x00449a60, FL 0x00453070), of the response routine and of
    the hull subtractor, and the push census of the "MakeDamage" string;
  * a raw scan of .text for any rel8/rel32 branch (70-7F, EB, E0-E3, E8, E9, 0F 80-8F) and any abs32 pointer that
    lands in the interior of a patched span, and of rel32 branches from outside into the contact block;
  * FL only: no base relocation entry inside any patched span or compared window;
  * no instruction inside the collision code writes the hull field [reg+0xec] (objdump decode);
  * the KC handler 2004::MakeDamage of the story object each install loads: its length and the
    zero-damage early return (decoded with verification/results/ship-navigation-ai/gate-queue/kc_obj.py).
Writes verify_collision_damage_output.json beside itself, prints PASS or FAIL; exit 0 only on PASS.
No Wine, nothing launched, no game bytes written.

    python3 -I verification/results/collision-damage/verify_collision_damage.py [AP exe] [FL exe]
"""
import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'verification/results/ship-navigation-ai/gate-queue'))
sys.path.insert(0, str(ROOT / 'tools/analysis'))

AP_EXE = Path(sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/X3/X3AP.exe')
FL_EXE = Path(sys.argv[2] if len(sys.argv) > 2 else '/mnt/c/X3FL/X3FL.exe')

ED = {
    'ap': dict(
        exe=AP_EXE,
        sha='fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab',
        response=(0x0045e130, 0x0045eea5),
        collision_code=(0x0045c780, 0x0045eea5),
        contact_block=(0x0045e6de, 0x0045ec5f),
        damage_fn=0x00449a60, hull_fn=0x0044ec10,
        damage_callers=[0x0044fab1, 0x0045dc43, 0x0045e383, 0x0045e433, 0x0045e569, 0x0045e621, 0x0045ebb8,
                        0x0045ebc2, 0x0045f054, 0x00463518],
        response_callers=[0x0045d233, 0x0045d2ff, 0x0045d691, 0x0045da3c, 0x0045df5c],
        hull_callers=[0x00449c52, 0x00449cc9],
        makedamage_str=0x0055fd58, makedamage_push=[0x0045dbf4, 0x0045eb49, 0x0045eb7e],
        windows=[
            # (name, VA, hex)
            ('contact_gate', 0x0045e6b0, '0fb74348 6685c0 0f84a2050000 663d0a00 0f8498050000 0fb74648 6685c0 '
                                         '0f848b050000 663d0a00 0f8481050000'),
            ('strength_and_floor', 0x0045e9f7, '8bc3 e89201ffff 03c0'),
            ('contact_law', 0x0045ea6f, '8b442 40c 8b54241c b9a0860100 f7ea f7f9'),
            ('contact_join_site', 0x0045eaf4, '8bc3 e885 14feff 84c0 750f'),
            ('contact_damage_calls', 0x0045ebaf, '8b442410 6aff 50 56 53 e8a3aefeff 6aff 57 53 56 e899aefeff'),
            ('planet_gate_site', 0x0045dbaf, '85f6 0f8ed9030000 8b7c241c'),
            ('planet_damage_call', 0x0045dc3a, '8b542424 6aff 56 52 57 e818befeff'),
        ],
        # patched spans: (name, first VA, length)
        spans=[('contact_join_site', 0x0045eaf4, 7), ('planet_gate_site', 0x0045dbaf, 2)],
        kc=('/mnt/c/X3/addon/07.cat', 'L/x3story.obj', 'mayhem 07.cat (the object this install loads)'),
        kc_extra=[('/mnt/c/X3/addon/04.cat', 'L/x3story.obj', 'stock AP 04.cat')],
    ),
    'fl': dict(
        exe=FL_EXE,
        sha='e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8',
        response=(0x00467f10, 0x00468dad),
        collision_code=(0x00466660, 0x00468dad),
        contact_block=(0x004685cc, 0x00468b79),
        damage_fn=0x00453070, hull_fn=0x004584b0,
        damage_callers=[0x004593ed, 0x0046797d, 0x00468185, 0x004682b1, 0x004683eb, 0x00468517, 0x00468b16,
                        0x00468b24, 0x00468f41, 0x0046d82b],
        response_callers=[0x00466eac, 0x00467377, 0x00467d31],
        hull_callers=[0x0045325f, 0x004532a4],
        makedamage_str=0x00588990, makedamage_push=[0x0046790d, 0x00468aa8, 0x00468adb],
        windows=[
            ('contact_gate', 0x0046859e, '0fb74648 6685c0 0f84ce050000 6683f80a 0f84c4050000 0fb74748 6685c0 '
                                         '0f84b7050000 6683f80a 0f84ad050000'),
            ('contact_law', 0x004689cb, '8b442450 8b54245c b9a0860100 f7ea f7f9'),
            ('contact_join_site', 0x00468a58, '8b4640 2500000c00 894c2420 3d00000400'),
            ('contact_damage_calls', 0x00468b0d, '6aff ff742414 8bce 57 e855a5feff 6aff ff742424 8bcf 56 e847a5feff'),
            ('planet_gate_site', 0x004678b7, '894c2424 85c9 0f8ebb050000 8b4640'),
            ('planet_damage_call', 0x00467970, '8b4c2424 6aff 51 ff742428 8bce e8eeb6feff'),
        ],
        spans=[('contact_join_site', 0x00468a58, 8), ('planet_gate_site', 0x004678bb, 2)],
        kc=('/mnt/c/X3FL/addon2/03.cat', 'addon2/L/x3story.obj', 'FL addon2/03.cat'),
        kc_extra=[],
    ),
}


class PE:
    def __init__(self, path):
        self.d = d = path.read_bytes()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        n = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
        self.secs = []
        for i in range(n):
            o = pe + 24 + opt + 40 * i
            vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
            self.secs.append((d[o:o + 8].rstrip(b'\0').decode(), self.base + va, vs, ro, rs))
        t = [s for s in self.secs if s[0] == '.text'][0]
        self.tva, self.text = t[1], d[t[3]:t[3] + t[4]]
        self.reloc_rva, self.reloc_size = struct.unpack_from('<II', d, pe + 24 + 96 + 5 * 8)

    def read(self, va, n):
        for _, v, vs, ro, rs in self.secs:
            if v <= va < v + rs:
                return self.d[ro + va - v:ro + va - v + n]
        raise ValueError(hex(va))

    def calls_to(self, target):
        t, out = self.text, []
        for k in range(len(t) - 5):
            if t[k] == 0xe8 and (self.tva + k + 5 + struct.unpack_from('<i', t, k + 1)[0]) & 0xffffffff == target:
                out.append(self.tva + k)
        return out

    def pushes_of(self, va):
        pat, out, k = b'\x68' + struct.pack('<I', va), [], -1
        while True:
            k = self.text.find(pat, k + 1)
            if k < 0:
                return out
            out.append(self.tva + k)

    def raw_branches_into(self, lo, hi, exclude=None):
        """Raw candidates: every rel8/rel32 branch encoding and abs32 dword in .text whose target is in [lo, hi)."""
        t, hits = self.text, []
        for k in range(len(t) - 1):
            b = t[k]
            src = self.tva + k
            if exclude and exclude[0] <= src < exclude[1]:
                continue
            if 0x70 <= b <= 0x7f or b == 0xeb or 0xe0 <= b <= 0xe3:
                tgt = src + 2 + struct.unpack_from('<b', t, k + 1)[0]
                if lo <= tgt < hi:
                    hits.append(('rel8', hex(src), hex(tgt)))
            if b in (0xe8, 0xe9) and k + 5 <= len(t):
                tgt = (src + 5 + struct.unpack_from('<i', t, k + 1)[0]) & 0xffffffff
                if lo <= tgt < hi:
                    hits.append(('rel32', hex(src), hex(tgt)))
            if b == 0x0f and k + 6 <= len(t) and 0x80 <= t[k + 1] <= 0x8f:
                tgt = (src + 6 + struct.unpack_from('<i', t, k + 2)[0]) & 0xffffffff
                if lo <= tgt < hi:
                    hits.append(('jcc32', hex(src), hex(tgt)))
        for k in range(len(t) - 3):
            v = struct.unpack_from('<I', t, k)[0]
            if lo <= v < hi:
                hits.append(('abs32', hex(self.tva + k), hex(v)))
        return hits

    def relocs_in(self, lo, hi):
        if not self.reloc_size:
            return []
        rel = self.read(self.base + self.reloc_rva, self.reloc_size)
        out, p = [], 0
        while p + 8 <= len(rel):
            page, size = struct.unpack_from('<II', rel, p)
            if size < 8:
                break
            for q in range(p + 8, p + size, 2):
                e = struct.unpack_from('<H', rel, q)[0]
                if e >> 12:
                    va = self.base + page + (e & 0xfff)
                    if lo - 3 <= va < hi:
                        out.append(hex(va))
            p += size
        return out


def objdump(exe, lo, hi):
    r = subprocess.run(['i686-w64-mingw32-objdump', '-d', '-M', 'intel', '--start-address=%#x' % lo,
                        '--stop-address=%#x' % hi, str(exe)], capture_output=True, text=True, check=True)
    return [ln for ln in r.stdout.splitlines() if re.match(r'^\s+[0-9a-f]+:\t', ln)]


def is_real_branch(pe, exe, src):
    """Decode from the enclosing function start (first byte after int3 padding) and report whether src is an
    instruction boundary carrying a branch; raw hits that are not are misaligned byte patterns."""
    k = src - pe.tva
    while k > 0 and not (pe.text[k - 1] == 0xcc and pe.text[k] != 0xcc):
        k -= 1
    for ln in objdump(exe, pe.tva + k, src + 8):
        a = int(ln.split(':')[0], 16)
        if a == src:
            return bool(re.search(r'\t(j\w+|call|loop\w*)\s', ln))
        if a > src:
            return False
    return False


def block_entries_within(exe, fn, block):
    """Decoded branches inside the routine fn that target the block from outside it."""
    out = []
    for ln in objdump(exe, fn[0], fn[1]):
        m = re.match(r'\s+([0-9a-f]+):\t.*\t(j\w+)\s+0x([0-9a-f]+)', ln)
        if m:
            src, tgt = int(m.group(1), 16), int(m.group(3), 16)
            if block[0] <= tgt < block[1] and not block[0] <= src < block[1]:
                out.append(hex(src))
    return out


def hull_writes(exe, lo, hi):
    """Instructions whose destination operand is [reg+0xec]."""
    out = []
    for ln in objdump(exe, lo, hi):
        parts = ln.split('\t')
        if len(parts) < 3:
            continue
        ins = parts[2].strip()
        m = re.match(r'(\w+)\s+(.*)', ins)
        if not m or m.group(1) in ('cmp', 'test', 'push', 'lea'):
            continue
        dst = m.group(2).split(',')[0]
        if re.search(r'\[\w+\+0xec\]', dst):
            out.append(parts[0].strip().rstrip(':') + ' ' + ins)
    return out


def kc_makedamage(cat, member):
    from kc_obj import load_member, Obj, MN
    o = Obj(load_member(cat, member))
    entries = o.find(2004, 'MakeDamage')
    res = {'build': o.build, 'entries': [hex(e) for e in entries]}
    if len(entries) != 1:
        res['ok'] = False
        return res
    ins = list(o.insns(entries[0]))
    seq = [(pc, MN.get(o.code[pc], 'op%02x' % o.code[pc]), o.operand(pc)) for pc in ins]
    res['length'] = len(seq)
    names = [s[1] for s in seq]
    natives = sorted({s[2] for s in seq if s[1] in ('native', 'op87', 'callself', 'callcls')})
    res['calls'] = natives
    if len(seq) <= 12:
        # stub: no call other than IsInvincible, only "push0; ret" exits
        res['zero_effect'] = natives == ['IsInvincible'] and names.count('ret') >= 1
        res['ok'] = res['zero_effect']
        return res
    # full handler: find "ldloc 9; push0; le; jt X", X = "push1", followed by "jf Y; dropn; push0; ret"
    found = False
    at = {pc: i for i, (pc, _, _) in enumerate(seq)}
    for i in range(len(seq) - 4):
        if [s[1] for s in seq[i:i + 4]] == ['ldloc', 'push0', 'le', 'jt'] and seq[i][2] == '9':
            x = int(seq[i + 3][2], 16)
            if x in at:
                j = at[x]
                tail = [s[1] for s in seq[j:j + 5]]
                if tail == ['push1', 'jf', 'dropn', 'push0', 'ret']:
                    found = True
                    # every instruction before the guard is a getter call, a compare/branch or a store
                    pre = {s[2] for s in seq[:i] if s[1] in ('native', 'op87', 'callself', 'callcls')}
                    res['calls_before_guard'] = sorted(pre)
    res['zero_damage_returns_early'] = found
    res['ok'] = found and set(res.get('calls_before_guard', [])) <= {
        'IsInvincible', '2004::GetShield', '2004::GetMaxShield', '2004::GetHull', '2004::GetMaxHull'}
    return res


def main():
    out, ok = {}, True
    for name, e in ED.items():
        pe = PE(e['exe'])
        r = {'exe': str(e['exe'])}
        r['sha256'] = hashlib.sha256(pe.d).hexdigest()
        r['sha_ok'] = r['sha256'] == e['sha']
        lo, hi = e['response']
        r['response'] = {'va': hex(lo), 'size': hi - lo,
                         'sha256': hashlib.sha256(pe.read(lo, hi - lo)).hexdigest()}
        r['windows'] = {}
        for wn, va, hx in e['windows']:
            want = bytes.fromhex(hx.replace(' ', ''))
            got = pe.read(va, len(want))
            rel = pe.relocs_in(va, va + len(want))
            r['windows'][wn] = {'va': hex(va), 'len': len(want), 'relocs': rel, 'ok': got == want and not rel}
            if got != want:
                r['windows'][wn]['got'] = got.hex()
        dc = pe.calls_to(e['damage_fn'])
        rc = pe.calls_to(e['response'][0])
        hc = pe.calls_to(e['hull_fn'])
        mp = pe.pushes_of(e['makedamage_str'])
        r['census'] = {
            'damage_fn': [hex(x) for x in dc], 'damage_ok': dc == e['damage_callers'],
            'response': [hex(x) for x in rc], 'response_ok': rc == e['response_callers'],
            'hull_fn': [hex(x) for x in hc], 'hull_ok': hc == e['hull_callers'],
            'makedamage_push': [hex(x) for x in mp], 'makedamage_ok': mp == e['makedamage_push'],
            'makedamage_string': pe.read(e['makedamage_str'], 11) == b'MakeDamage\0',
        }
        r['spans'] = {}
        for sn, va, n in e['spans']:
            hits = pe.raw_branches_into(va + 1, va + n)
            rel = pe.relocs_in(va, va + n)
            r['spans'][sn] = {'va': hex(va), 'len': n, 'interior_raw_hits': hits, 'relocs': rel,
                              'ok': not hits and not rel}
        blo, bhi = e['contact_block']
        outside = [h for h in pe.raw_branches_into(blo, bhi, exclude=(e['response'][0], e['response'][1]))
                   if h[0] in ('rel32', 'jcc32')]
        r['contact_block_raw_candidates_from_outside'] = outside
        outside = [h for h in outside if is_real_branch(pe, e['exe'], int(h[1], 16))]
        r['contact_block_entries_from_outside'] = outside
        r['contact_block_entries_within_response'] = block_entries_within(e['exe'], e['response'], (blo, bhi))
        outside = outside + r['contact_block_entries_within_response']
        clo, chi = e['collision_code']
        r['hull_writes_in_collision_code'] = hull_writes(e['exe'], clo, chi)
        r['kc'] = {e['kc'][2]: kc_makedamage(e['kc'][0], e['kc'][1])}
        for cat, mem, label in e['kc_extra']:
            r['kc'][label] = kc_makedamage(cat, mem)
        r['ok'] = (r['sha_ok'] and all(w['ok'] for w in r['windows'].values())
                   and all(v for k, v in r['census'].items() if k.endswith('_ok') or k == 'makedamage_string')
                   and all(s['ok'] for s in r['spans'].values()) and not outside
                   and not r['hull_writes_in_collision_code'] and all(k['ok'] for k in r['kc'].values()))
        ok &= r['ok']
        out[name] = r
    out['result'] = 'PASS' if ok else 'FAIL'
    (HERE / 'verify_collision_damage_output.json').write_text(json.dumps(out, indent=1) + '\n')
    print(out['result'])
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
