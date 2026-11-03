#!/usr/bin/env python3
"""Read-only qualification of the FL collision box-cull sites in X3FL.exe (src/engine/fl/collide_box_cull_sites_fl.h,
collide_box_cull_fl.cpp, the box table of collide_windows_fl_inc.h).

docs/reverse-engineering/fl/collision-box.md, "Port (2026-11-03)". On the installed exe (default /mnt/c/X3FL/X3FL.exe,
SHA-256 e7f3aad8..., preferred base 0x400000, every value an RVA) it checks:

- the nine windows: hash with HIGHLOW dwords zeroed, the relocation census equal to the declared fixups (none),
  whole-instruction spans, single-byte and relocation mutations refused, admitted at a rebased base;
- both sites: two whole MOVs with the header's bytes (P1 MOV ECX,[EDI+0x70]; MOV EDX,[EBX+0x70], P2 MOV EAX,[EBX+0x70];
  MOV EDX,[EDI+0x70]), the native distance after them in scalar SSE only (3 CVTDQ2PS, 3 MULSS, 2 ADDSS, the call of the
  sqrt helper, CVTTSS2SI; no x87), the native threshold and compare (P1 (r*0x1028f+0x8000)>>16, JG to the class-7
  rejection context; P2 radius + [ESP+0x2c], ESI = [ESP+0x14], JG to the continuation), ESP unchanged between the site
  and the threshold reads (the stub reads the same [ESP+0x34] / [ESP+0x2c] / [ESP+0x14]);
- the pair/class word layout: radius sum A+0xa4 + B+0xa4 stored to [ESP+0x34]; the rejection context 0x0673ae tests the
  class words [EDI+0x48] and [EBX+0x48] and only when neither is 7 jumps to the continuation 0x067e7e (class 7 on either
  object reuses the distance: exempt); the class-7 entry routes non-7 pairs to the site;
- the sqrt helper: SQRTSS XMM0,XMM0 between an aligned frame, plain RET, no x87;
- interior entries: no raw rel8/rel32 branch in .text and no relocated pointer lands inside either six-byte site;
  decoded direct inbound edges to each site exactly the contract's;
- liveness at both reject continuations over every path (jump tables followed): EAX/ECX/EDX/EFLAGS, XMM0-2 and the
  omitted stack slots (P1 distance [ESP+0x14]; P2 [ESP+0x3c/+0x20/+0x50/+0x40]) are written before any read; the
  calls reached first are exactly the contract's (their XMM inputs: three decoded XMM-free, two by contract);
- qword geometry: P1's five jump bytes cross a qword (plain write in the install window), P2's lie in one (atomic);
- the AP addresses are refused (the windows at AP's site RVAs in this image, and on the AP exe when present).

Writes one JSON report (result PASS/FAIL) to stdout or --out. No Wine, no game launch, nothing written to the exe.

usage: python3 verification/probe/verify_collide_box_cull_sites_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fl_collide_sites_common as C  # noqa: E402

SITES = C.ROOT / 'src/engine/fl/collide_box_cull_sites_fl.h'
BASE = C.BASE
P1_FUNCTION = (0x066de0, 0x067e9c)
P2_FUNCTION = (0x066660, 0x066b4a)
P1_SITE, P1_CONTINUE, P1_REJECT = 0x06723f, 0x067e7e, 0x0673ae
P2_SITE, P2_CONTINUE = 0x066859, 0x066a1b
SQRT = 0x012a80
AP_SITE_RVAS = (0x05d58e, 0x05cc7c, 0x05df90, 0x05ce07)  # AP P1/P2 sites and continuations as RVAs
# The calls first reached from the continuations with XMM0-2 still undefined (collision-box.md, "Keep/reject state"):
XMM_FREE_DECODED = (0x012c60, 0x066b50, 0x0a61a0)  # integer helpers: their bodies decoded, no XMM read
XMM_BY_CONTRACT = (0x0bb500, 0x12b510)  # named dispatch (integer cells, cdecl) and CRT qsort (pointer/int callback)
EXPECTED_CALLS = {'p1': {0x0bb500, 0x012c60}, 'p2': {0x12b510, 0x0a61a0, 0x066b50}}


def header_bytes(name):
    text = SITES.read_text()
    body = re.search(r'constexpr unsigned char %s\[site_length\] = \{(.*?)\};' % name, text, re.S).group(1)
    return bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', body))


def ops(insns):
    return [(i.address - BASE, i.mnemonic, i.op_str) for i in insns]


def xmm_free(im, target):
    """No instruction of the callee's linear body up to its first RET reads an XMM register."""
    for i in im.md.disasm(im.read(target, 0x400), BASE + target):
        regs_read, _ = i.regs_access()
        if any(i.reg_name(r).startswith('xmm') for r in regs_read):
            return False
        if i.mnemonic == 'ret':
            return True
    return False


def esp_balanced(insns):
    return not any(i.mnemonic in ('push', 'pop') or (i.mnemonic in ('add', 'sub', 'lea') and i.op_str.startswith('esp'))
                   for i in insns)


def verify(data):
    im = C.Image(data)
    checks = {}
    report = {"exe_info": C.report_exe(im), "checks": checks}

    def check(name, ok, **info):
        checks[name] = {'ok': bool(ok), **info}

    ok, rows = C.check_windows(im, 'box')
    check('windows', ok, count=len(rows), rows=rows)
    refused, total = C.mutation_controls(im, 'box')
    check('window_mutations', refused == total, refused=refused, total=total)
    p1_bytes, p2_bytes = header_bytes('p1_site'), header_bytes('p2_site')
    # ---- P1: the site, the native distance, threshold and compare ----
    p1, p1_ok = im.insns(P1_SITE, 0x0672af)
    o = ops(p1)
    check('p1_site', p1_ok and im.read(P1_SITE, 6) == p1_bytes and o[0][1:] == ('mov', 'ecx, dword ptr [edi + 0x70]')
          and o[1][1:] == ('mov', 'edx, dword ptr [ebx + 0x70]') and o[2][0] == P1_SITE + 6, first=o[:3])
    mn = [i.mnemonic for i in p1]
    check('p1_sse_distance', mn.count('cvtdq2ps') == 3 and mn.count('mulss') == 3 and mn.count('addss') == 2 and
          mn.count('cvttss2si') == 1 and not any(C.is_x87_or_mmx(i) for i in p1) and
          ('call', hex(BASE + SQRT)) in [(i.mnemonic, i.op_str) for i in p1])
    check('p1_threshold_compare', ('mov', 'eax, dword ptr [esp + 0x34]') in [x[1:] for x in o] and
          ('mov', 'edx, 0x1028f') in [x[1:] for x in o] and ('add', 'eax, 0x8000') in [x[1:] for x in o] and
          ('shrd', 'eax, edx, 0x10') in [x[1:] for x in o] and o[-2][1:] == ('cmp', 'ecx, eax') and
          o[-1][1:] == ('jg', hex(BASE + P1_REJECT)) and esp_balanced(p1))
    radius, r_ok = im.insns(0x06703d, 0x067051)
    check('radius_sum', r_ok and ops(radius)[0][1:] == ('mov', 'edx, dword ptr [edi + 0xa4]') and
          ops(radius)[1][1:] == ('add', 'edx, dword ptr [ebx + 0xa4]') and
          ops(radius)[3][1:] == ('mov', 'dword ptr [esp + 0x34], edx'))
    rej, j_ok = im.insns(P1_REJECT, 0x0673c3)
    r = ops(rej)
    check('class7_exempt_either_object', j_ok and r[0][1:] == ('movzx', 'ecx, word ptr [edi + 0x48]') and
          r[1][1:] == ('cmp', 'cx, 7') and r[2][1:] == ('je', hex(BASE + 0x0673c3)) and
          r[3][1:] == ('cmp', 'word ptr [ebx + 0x48], 7') and r[4][1:] == ('jne', hex(BASE + P1_CONTINUE)), rows=r)
    entry, e_ok = im.insns(0x0671b1, 0x0671c1)
    e = ops(entry)
    check('class7_entry', e_ok and e[0][1:] == ('cmp', 'ax, 7') and e[2][1:] == ('cmp', 'cx, 7') and
          e[3][1:] == ('jne', hex(BASE + P1_SITE)))
    # ---- P2 ----
    p2, p2_ok = im.insns(P2_SITE, 0x0668cb)
    o2 = ops(p2)
    check('p2_site', p2_ok and im.read(P2_SITE, 6) == p2_bytes and o2[0][1:] == ('mov', 'eax, dword ptr [ebx + 0x70]')
          and o2[1][1:] == ('mov', 'edx, dword ptr [edi + 0x70]'), first=o2[:3])
    mn2 = [i.mnemonic for i in p2]
    check('p2_sse_distance', mn2.count('cvtdq2ps') == 3 and mn2.count('mulss') == 3 and mn2.count('addss') == 2 and
          mn2.count('cvttss2si') == 1 and not any(C.is_x87_or_mmx(i) for i in p2) and
          ('call', hex(BASE + SQRT)) in [(i.mnemonic, i.op_str) for i in p2])
    flat2 = [x[1:] for x in o2]
    check('p2_threshold_compare', ('mov', 'eax, dword ptr [ebx + 0xa4]') in flat2 and
          ('add', 'eax, dword ptr [esp + 0x2c]') in flat2 and ('mov', 'esi, dword ptr [esp + 0x14]') in flat2 and
          o2[-2][1:] == ('cmp', 'ecx, eax') and o2[-1][1:] == ('jg', hex(BASE + P2_CONTINUE)) and esp_balanced(p2))
    # the count ESI the continuation reads (0x066a49 `cmp esi, 1`)
    cont2, _ = im.insns(P2_CONTINUE, 0x066a4c)
    check('p2_count_read', ops(cont2)[-1][1:] == ('cmp', 'esi, 1'))
    # ---- the sqrt helper ----
    s, s_ok = im.insns(SQRT, SQRT + 14)
    check('sqrt_helper', s_ok and [i.mnemonic for i in s] == ['push', 'mov', 'and', 'sqrtss', 'mov', 'pop', 'ret'] and
          s[3].op_str == 'xmm0, xmm0' and not any(C.is_x87_or_mmx(i) for i in s))
    # ---- interior entries and inbound edges ----
    inner = C.raw_branch_hits(im, P1_SITE, P1_SITE + 6) + C.raw_branch_hits(im, P2_SITE, P2_SITE + 6)
    pointers = C.abs_pointers_into(im, P1_SITE + 1, P1_SITE + 6) + C.abs_pointers_into(im, P2_SITE + 1, P2_SITE + 6)
    check('no_interior_entry', not inner and not pointers, raw=[(hex(a), hex(b)) for a, b in inner],
          pointers=[hex(p) for p in pointers])
    in1 = C.decoded_inbound(im, *P1_FUNCTION, P1_SITE)
    in2 = C.decoded_inbound(im, *P2_FUNCTION, P2_SITE)
    check('inbound_edges', in1 == [0x0671bb] and sorted(in2) == [0x06683b, 0x06684d],
          p1=[hex(x) for x in in1], p2=[hex(x) for x in in2])
    # ---- liveness at the reject continuations ----
    base_items = {'eax', 'ecx', 'edx', 'eflags', 'xmm0', 'xmm1', 'xmm2'}
    v1, steps1, paths1 = C.explore(im, P1_CONTINUE, base_items | {('stk', 0x14)}, P1_FUNCTION)
    v2, steps2, paths2 = C.explore(im, P2_CONTINUE, base_items | {('stk', 0x3c), ('stk', 0x20), ('stk', 0x50),
                                                                   ('stk', 0x40)}, P2_FUNCTION)

    def split(violations):
        calls = {int(a, 16) - BASE for a, what, items in violations
                 if what.startswith('call 0x') and set(items) <= {'xmm0', 'xmm1', 'xmm2'}}
        targets = set()
        for a, what, items in violations:
            if what.startswith('call 0x') and set(items) <= {'xmm0', 'xmm1', 'xmm2'}:
                targets.add(int(what.split()[1], 16) - BASE)
        other = [x for x in violations if not (x[1].startswith('call 0x') and set(x[2]) <= {'xmm0', 'xmm1', 'xmm2'})]
        return targets, other, calls
    t1, other1, _ = split(v1)
    t2, other2, _ = split(v2)
    decoded_free = {t: xmm_free(im, t) for t in XMM_FREE_DECODED}
    check('reject_liveness', not other1 and not other2 and t1 == EXPECTED_CALLS['p1'] and t2 == EXPECTED_CALLS['p2']
          and all(decoded_free.values()), p1_paths=paths1, p1_steps=steps1, p2_paths=paths2, p2_steps=steps2,
          p1_first_calls=sorted(hex(t) for t in t1), p2_first_calls=sorted(hex(t) for t in t2),
          other=[*other1, *other2][:10], xmm_free_decoded={hex(k): v for k, v in decoded_free.items()},
          xmm_by_contract=[hex(t) for t in XMM_BY_CONTRACT])
    # ---- qword geometry ----
    check('qword_geometry', (P1_SITE & 7) + 5 > 8 and (P2_SITE & 7) + 5 <= 8,
          p1_qword_offset=P1_SITE & 7, p2_qword_offset=P2_SITE & 7)
    # ---- the AP addresses refused ----
    ap = C.ap_refusal(im, 'box', AP_SITE_RVAS)
    ap_site_bytes = all(im.read(r, 6) not in (p1_bytes, p2_bytes) for r in AP_SITE_RVAS[:2])
    check('ap_addresses_refused', ap['all_refused'] and ap_site_bytes, **ap)
    report['result'] = 'PASS' if all(c['ok'] for c in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=C.DEFAULT_EXE)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe.read_bytes())
    text = json.dumps(report, indent=1)
    if args.out:
        args.out.write_text(text + '\n')
    print(json.dumps({'result': report['result'], 'sha256': report['exe_info']['sha256'],
                      'failed': [k for k, v in report['checks'].items() if not v['ok']],
                      'checks': len(report['checks'])}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
