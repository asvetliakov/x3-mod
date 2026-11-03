#!/usr/bin/env python3
"""Read-only proof that X3FL.exe's OBB separating-axis test is native scalar SSE, so the FL collide_sat_sse2 patches
nothing (src/engine/fl/collide_sat_sse2_fl.cpp, collide_sat_sse2_sites_fl.h, the sat table of collide_windows_fl_inc.h).

docs/reverse-engineering/fl/collision-sat.md, "Port decision (2026-10-29)" and "Port (2026-11-03)". On the installed exe
(default /mnt/c/X3FL/X3FL.exe, SHA-256 e7f3aad8..., preferred base 0x400000) it checks:

- the two windows (the SAT body base+0x11c6d0, 0x67b bytes, its two HIGHLOW operands; the descent's call window): hash,
  relocation census, whole instructions, mutations refused, admitted rebased;
- the body is scalar SSE only: no x87 or MMX instruction, no LDMXCSR/STMXCSR/FXRSTOR/FLDCW, no call, no FS segment;
  the study's counts 81 MULSS, 60 ADDSS, 9 SUBSS, 24 ANDPS, 15 COMISS and 15 plain RETs; the frame (PUSH EBP; MOV EBP,ESP;
  AND ESP,-8) and ECX/EDX taken as R/T;
- the caller: the one encoded direct call in .text is the descent's at base+0x11a744, no relocated pointer to the entry,
  no branch into the call's operand, the caller drops 8 bytes and tests EAX (zero/nonzero is all it consumes);
- the AP addresses are refused (the windows at AP's SAT and call RVAs in this image, and on the AP exe when present).

Writes one JSON report (result PASS/FAIL). No Wine, no game launch, nothing written to the exe.

usage: python3 verification/probe/verify_collide_sat_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fl_collide_sites_common as C  # noqa: E402

BASE = C.BASE
SAT, SAT_END = 0x11c6d0, 0x11cd4b
CALL, CALL_RETURN = 0x11a744, 0x11a749
AP_RVAS = (0x0e3280, 0x0e25a3, 0x0e259d)  # AP SAT entry, its call site, AP's call window start
COUNTS = {'mulss': 81, 'addss': 60, 'subss': 9, 'andps': 24, 'comiss': 15, 'ret': 15}


def verify(data):
    im = C.Image(data)
    checks = {}
    report = {'exe_info': C.report_exe(im), 'checks': checks}

    def check(name, ok, **info):
        checks[name] = {'ok': bool(ok), **info}

    ok, rows = C.check_windows(im, 'sat')
    check('windows', ok, count=len(rows), rows=rows)
    refused, total = C.mutation_controls(im, 'sat')
    check('window_mutations', refused == total, refused=refused, total=total)
    body, whole = im.insns(SAT, SAT_END)
    counts = {m: sum(1 for i in body if i.mnemonic == m) for m in COUNTS}
    x87 = [hex(i.address) for i in body if C.is_x87_or_mmx(i)]
    forbidden = [hex(i.address) for i in body if i.mnemonic in ('ldmxcsr', 'stmxcsr', 'fxrstor', 'fldcw', 'call') or
                 'fs:' in i.op_str]
    check('native_scalar_sse', whole and not x87 and not forbidden and counts == COUNTS,
          instructions=len(body), counts=counts, x87_or_mmx=x87, forbidden=forbidden)
    head = [(i.mnemonic, i.op_str) for i in body[:3]]
    rets = [i for i in body if i.mnemonic == 'ret']
    check('frame', head == [('push', 'ebp'), ('mov', 'ebp, esp'), ('and', 'esp, 0xfffffff8')] and
          all(not i.operands for i in rets), head=head)
    callers = C.direct_callers(im, SAT)
    pointers = sorted(f for f in im.fixups if im.u32(f) - BASE == SAT)
    inner = C.raw_branch_hits(im, CALL, CALL_RETURN)
    check('sole_caller', callers == [CALL] and not pointers and not inner, callers=[hex(c) for c in callers],
          pointers=[hex(p) for p in pointers], interior=[(hex(a), hex(b)) for a, b in inner])
    after, a_ok = im.insns(CALL, 0x11a750)
    check('caller_consumes_zero_test', a_ok and [(i.mnemonic, i.op_str) for i in after][:3] ==
          [('call', hex(BASE + SAT)), ('add', 'esp, 8'), ('test', 'eax, eax')])
    ap = C.ap_refusal(im, 'sat', AP_RVAS)
    check('ap_addresses_refused', ap['all_refused'], **ap)
    report['result'] = 'PASS' if all(c['ok'] for c in checks.values()) else 'FAIL'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', type=Path, default=C.DEFAULT_EXE)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = verify(args.exe.read_bytes())
    if args.out:
        args.out.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps({'result': report['result'], 'sha256': report['exe_info']['sha256'],
                      'failed': [k for k, v in report['checks'].items() if not v['ok']],
                      'checks': len(report['checks'])}))
    return 0 if report['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
