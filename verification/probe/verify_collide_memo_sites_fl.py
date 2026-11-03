#!/usr/bin/env python3
"""Read-only qualification of the FL no-contact memo's engine boundary in X3FL.exe (src/engine/fl/collide_memo_fl.cpp,
collide_memo_sites_fl.h, the memo table of collide_windows_fl_inc.h).

docs/reverse-engineering/fl/collision-memo.md, "Port (2026-11-03)". On the installed exe (default /mnt/c/X3FL/X3FL.exe,
SHA-256 e7f3aad8..., preferred base 0x400000, every value an RVA) it checks:

- the fourteen bodies (caller, query, descent, leaf, triangle test and projection, SAT, two matrix products, five model
  mutators): hash with every HIGHLOW dword zeroed and bound to its target, census, whole instructions, mutations
  refused, admitted rebased;
- the site: `call query` at base+0x09853a, the sole encoded direct call of the query in .text, no relocated pointer to
  its entry, no branch into the call operand, five bytes in one aligned qword; PUSH EBX (model A) before it, ADD ESP,0x18;
  XOR EAX,EAX; CMP [contacts],EAX; SETNE AL after it (the caller consumes only contacts != 0);
- the key inputs: ECX = LEA [ESP+0x54] (R1), EDX = LEA [ESP+0x24] (T1), XMM2 = CVTDQ2PS of [ESI+0x70] (s1) last written
  before the call; six pushes from the masked mode word to the call (model A, &R2, &T2, s2, model B, flags & ~0xc); the
  caller writes cap, flags, tolerance (CVTTSS2SI) and the minimum pointer before the call; the query takes ECX -> EBP,
  EDX -> ESI, spills XMM2, tests the init flag, both models' +0x14 == 3 at entry+4 / entry+0x14, derives the
  first-contact flag from entry+0x18 == 2, loads both roots from +0;
- the writes a query can make (every relocated operand of the closure classified): written globals are exactly the
  replayed set (visits, triangle tests, contacts, first-contact flag, root T/R/scale), the init-only pair (only inside
  the init block) and the contact record (only after the leaf's intersection gate); read globals are the key inputs,
  that scratch, or read-only constants; non-stack indirect stores pinned per body, the *minimum store after the gate;
- the minimum relaxation: the only load of the minimum pointer is in the leaf, after the leaf's unconditional
  triangle-test increment, which is straight-line from the leaf entry;
- the model layout and the invalidation edges the key's header/root stamps rest on: the leaf reads box+0x44, the
  descent box+0x3c/+0x40; insert refuses state 3 (EAX=4), reset writes state 1, finish writes state 3, resource
  cleanup clears resource+0x5c right after the destroy call;
- the closure: no x87/MMX, no MXCSR/x87 control write, no FS, every direct call inside the closure;
- the AP addresses are refused (the AP site and target RVAs in this image, and the AP exe when present).

Writes one JSON report (result PASS/FAIL). No Wine, no game launch, nothing written to the exe.

usage: python3 verification/probe/verify_collide_memo_sites_fl.py [--exe X3FL.exe] [--out report.json]
"""
import argparse
import json
import sys
from pathlib import Path

import capstone
from capstone import x86_const as X

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fl_collide_sites_common as C  # noqa: E402

BASE = C.BASE
CALLER, SITE, RETURN, QUERY = 0x0982d0, 0x09853a, 0x09853f, 0x11aae0
CLOSURE = ('query', 'descent', 'leaf', 'triangle', 'projection', 'sat', 'transpose_product', 'matrix_product')
FLAGS, CAP, TOLERANCE, MINIMUM = 0x1a9044, 0x1aa0e0, 0x1aa09c, 0x1aa084
VISITS, TRIANGLES, CONTACTS = 0x1a9048, 0x1aa0d0, 0x1aa0a0
FIRST, ROOT_T, ROOT_R, SCALE, INIT, UNTOUCHED = 0x21b980, 0x21b988, 0x21b994, 0x21b9b8, 0x21b9bc, 0x21b984
REPLAYED = {VISITS, TRIANGLES, CONTACTS, FIRST, SCALE} | {ROOT_T + 4 * k for k in range(3)} | \
           {ROOT_R + 4 * k for k in range(9)}
INIT_ONLY = {UNTOUCHED, INIT}
CONTACT_RECORD = {0x1aa0a8 + 4 * k for k in range(6)}
KEY_INPUTS = {FLAGS, CAP, TOLERANCE, MINIMUM}
INIT_BLOCK = (0x11ab01, 0x11ab29)
CONTACT_GATE = (0x11a507, 0x11a6b4)  # the leaf after `test eax,eax; je` of the triangle test
MINIMUM_LOAD, MINIMUM_STORE, TRIANGLE_COUNT, LEAF = 0x11a5d9, 0x11a5eb, 0x11a43d, 0x11a260
AP_RVAS = (0x07f329, 0x0e29f0, 0x07f1b0, 0x0e2780)  # AP memo site, wrapper target, caller, query as RVAs


def body(im, name):
    w = next(x for x in C.windows('memo') if x[0] == name)
    return w[1], w[1] + w[2]


def rows(insns):
    return [(i.address - BASE, i.mnemonic, i.op_str) for i in insns]


def mem_globals(im, insn):
    """[(rva, access)] of every operand whose memory displacement or immediate is a relocated absolute address."""
    out, start = [], insn.address - BASE
    reloc = {f - start for f in im.fixups if start <= f < start + insn.size}
    if not reloc:
        return out
    for op in insn.operands:
        if op.type == X.X86_OP_MEM and not op.mem.base and not op.mem.index:
            out.append((op.mem.disp - BASE, op.access))
        elif op.type == X.X86_OP_MEM and (op.mem.base or op.mem.index) and insn.disp_offset in reloc:
            out.append((op.mem.disp - BASE, 'indexed'))
        elif op.type == X.X86_OP_IMM and insn.imm_offset in reloc:
            out.append((op.imm - BASE, 'address'))
    return out


def verify(data):
    im = C.Image(data)
    checks = {}
    report = {'exe_info': C.report_exe(im), 'checks': checks}

    def check(name, ok, **info):
        checks[name] = {'ok': bool(ok), **info}

    ok, wrows = C.check_windows(im, 'memo')
    check('windows', ok, count=len(wrows), rows=wrows)
    refused, total = C.mutation_controls(im, 'memo')
    check('window_mutations', refused == total, refused=refused, total=total)
    # ---- the site ----
    caller, c_ok = im.insns(*body(im, 'caller'))
    r = rows(caller)
    idx = next(k for k, x in enumerate(r) if x[0] == SITE)
    callers = C.direct_callers(im, QUERY)
    pointers = sorted(f for f in im.fixups if im.u32(f) - BASE == QUERY)
    inner = C.raw_branch_hits(im, SITE, RETURN)
    check('site', c_ok and r[idx][1:] == ('call', hex(BASE + QUERY)) and callers == [SITE] and not pointers and
          not inner and (SITE & 7) + 5 <= 8 and im.read(SITE, 5) == bytes.fromhex('e8a1250800'),
          callers=[hex(c) for c in callers], pointers=len(pointers), interior=len(inner), qword_offset=SITE & 7)
    tail = [x[1:] for x in r[idx + 1:idx + 5]]
    check('caller_consumes_contacts_only', r[idx - 1][1:] == ('push', 'ebx') and tail == [
        ('add', 'esp, 0x18'), ('xor', 'eax, eax'), ('cmp', 'dword ptr [%s], eax' % hex(BASE + CONTACTS)),
        ('setne', 'al')], tail=tail)

    # ---- key inputs at the call ----
    def last_writer(reg):
        for i in reversed(caller[:idx]):
            _, w = i.regs_access()
            if any(i.reg_name(x) == reg for x in w):
                return (i.address - BASE, i.mnemonic, i.op_str)
        return None
    ecx, edx, xmm2 = last_writer('ecx'), last_writer('edx'), last_writer('xmm2')
    xmm2_src = next(((i.mnemonic, i.op_str) for i in reversed(caller[:idx])
                     if i.mnemonic == 'movd' and i.op_str.startswith('xmm2,')), None)
    check('register_inputs', ecx and ecx[1:] == ('lea', 'ecx, [esp + 0x54]') and edx and
          edx[1:] == ('lea', 'edx, [esp + 0x24]') and xmm2 and xmm2[1:] == ('cvtdq2ps', 'xmm2, xmm2') and
          xmm2_src == ('movd', 'xmm2, dword ptr [esi + 0x70]'), ecx=ecx, edx=edx, xmm2=xmm2, xmm2_source=xmm2_src)
    mask = next(k for k, x in enumerate(r) if x[1:] == ('and', 'ecx, 0xfffffff3'))
    pushes = [x for x in r[mask + 1:idx] if x[1] == 'push']
    flat = [x[1:] for x in r[:idx]]
    literal = [('mov', 'dword ptr [%s], eax' % hex(BASE + CAP)), ('mov', 'dword ptr [%s], ecx' % hex(BASE + FLAGS)),
               ('mov', 'dword ptr [%s], eax' % hex(BASE + TOLERANCE)),
               ('mov', 'dword ptr [%s], eax' % hex(BASE + MINIMUM))]
    tol = next(k for k, x in enumerate(r) if x[1:] == literal[2])
    tol_src = next((x for x in reversed(r[:tol]) if x[2].startswith('eax')), None)
    check('stack_and_literal_inputs', len(pushes) == 6 and r[mask + 1][1:] == ('push', 'ecx') and
          all(x in flat for x in literal) and tol_src and tol_src[1] == 'cvttss2si',
          pushes=[x[2] for x in pushes], tolerance_source=tol_src)
    query, q_ok = im.insns(*body(im, 'query'))
    q = [x[1:] for x in rows(query)[:40]]
    need = [('cmp', 'dword ptr [%s], 0' % hex(BASE + INIT)), ('mov', 'esi, edx'),
            ('movss', 'dword ptr [esp + 0x10], xmm2'), ('mov', 'ebp, ecx'), ('mov', 'ebx, dword ptr [esp + 0xb0]'),
            ('cmp', 'dword ptr [ebx + 0x14], 3'), ('mov', 'edi, dword ptr [esp + 0xc0]'),
            ('cmp', 'dword ptr [edi + 0x14], 3'), ('mov', 'ebx, dword ptr [ebx]'), ('mov', 'edi, dword ptr [edi]'),
            ('cmp', 'dword ptr [esp + 0xc4], 2'), ('mov', 'dword ptr [%s], eax' % hex(BASE + FIRST))]
    frame = [x[1:] for x in rows(query)[:8]]
    check('query_entry', q_ok and all(x in q for x in need) and frame[0] == ('sub', 'esp, 0x9c') and
          [x[0] for x in frame[2:8]].count('push') == 4, missing=[x for x in need if x not in q])
    # ---- writes and reads of the closure ----
    written, read, misplaced, indirect, x87, calls_out = {}, set(), [], {}, [], []
    entries = {body(im, n)[0] for n in CLOSURE}
    for name in CLOSURE:
        lo, hi = body(im, name)
        insns, _ = im.insns(lo, hi)
        stores = []
        for i in insns:
            at = i.address - BASE
            if C.is_x87_or_mmx(i) or i.mnemonic in ('ldmxcsr', 'fldcw', 'fxrstor') or 'fs:' in i.op_str:
                x87.append(hex(i.address))
            if i.group(X.X86_GRP_CALL):
                op = i.operands[0]
                if op.type != X.X86_OP_IMM or op.imm - BASE not in entries:
                    calls_out.append(hex(i.address))
            for g, access in mem_globals(im, i):
                if access == 'address':
                    # an address taken: the root rotation's helper output (0x51af57's ECX) or a constant table
                    if C.read_only(im, g):
                        read.add(g)
                    else:
                        written.setdefault(g, []).append(at)
                    continue
                if access == 'indexed' or access & capstone.CS_AC_READ:
                    read.add(g)
                if access != 'indexed' and access & capstone.CS_AC_WRITE:
                    written.setdefault(g, []).append(at)
            for op in i.operands:
                if op.type == X.X86_OP_MEM and op.access & capstone.CS_AC_WRITE and op.mem.base and \
                        i.reg_name(op.mem.base) not in ('esp', 'ebp') and i.mnemonic not in ('push', 'call'):
                    stores.append(at)
        indirect[name] = stores
    root_r_block = {ROOT_R + 4 * k for k in range(9)}
    for g, ats in written.items():
        if g in INIT_ONLY:
            misplaced += [hex(a) for a in ats if not INIT_BLOCK[0] <= a < INIT_BLOCK[1]]
        elif g in CONTACT_RECORD:
            misplaced += [hex(a) for a in ats if not CONTACT_GATE[0] <= a < CONTACT_GATE[1]]
        elif g not in REPLAYED and g not in root_r_block:
            misplaced.append('%s:%s' % (hex(g), [hex(a) for a in ats]))
    writable_reads = {g for g in read if not C.read_only(im, g)}
    unknown_reads = sorted(hex(g) for g in writable_reads - KEY_INPUTS - REPLAYED - INIT_ONLY - CONTACT_RECORD -
                           root_r_block)
    check('closure_writes', not misplaced and set(written) <= REPLAYED | INIT_ONLY | CONTACT_RECORD | root_r_block and
          REPLAYED - {ROOT_R + 4 * k for k in range(1, 9)} <= set(written),
          written=sorted(hex(g) for g in written), misplaced=misplaced)
    check('closure_reads', not unknown_reads, unknown=unknown_reads,
          key_inputs_read=sorted(hex(g) for g in writable_reads & KEY_INPUTS))
    leaf_stores = indirect['leaf']
    check('indirect_stores', MINIMUM_STORE in leaf_stores and
          all(CONTACT_GATE[0] <= a < CONTACT_GATE[1] for a in leaf_stores if a == MINIMUM_STORE),
          per_body={k: len(v) for k, v in indirect.items()}, leaf=[hex(a) for a in leaf_stores])
    check('closure_cpu_state', not x87 and not calls_out, x87=x87, calls_out=calls_out)
    # ---- minimum relaxation ----
    loads = []
    for name in CLOSURE:
        for i in im.insns(*body(im, name))[0]:
            if any(g == MINIMUM for g, _ in mem_globals(im, i)):
                loads.append(i.address - BASE)
    straight, s_ok = im.insns(LEAF, TRIANGLE_COUNT)
    branchy = [hex(i.address) for i in straight if i.group(X.X86_GRP_JUMP) or i.group(X.X86_GRP_CALL) or
               i.group(X.X86_GRP_RET)]
    count = im.insn(TRIANGLE_COUNT)
    check('minimum_relaxation', loads == [MINIMUM_LOAD] and TRIANGLE_COUNT < MINIMUM_LOAD and s_ok and not branchy and
          (count.mnemonic, count.op_str) == ('inc', 'dword ptr [%s]' % hex(BASE + TRIANGLES)),
          loads=[hex(a) for a in loads], branches_before_count=branchy)
    # ---- model layout and invalidation edges ----
    leaf_rows = [x[1:] for x in rows(im.insns(*body(im, 'leaf'))[0])]
    descent_rows = [x[1:] for x in rows(im.insns(*body(im, 'descent'))[0])]
    insert_rows = [x[1:] for x in rows(im.insns(*body(im, 'insert_triangle'))[0])]
    reset_rows = [x[1:] for x in rows(im.insns(*body(im, 'reset_model'))[0])]
    finish_rows = [x[1:] for x in rows(im.insns(*body(im, 'finish_model'))[0])]
    cleanup, _ = im.insns(0x099815, 0x099826)
    cleanup_rows = [x[1:] for x in rows(cleanup)]
    check('model_layout_and_edges',
          ('mov', 'esi, dword ptr [ecx + 0x44]') in leaf_rows and ('mov', 'eax, dword ptr [ebx + 0x3c]') in descent_rows
          and ('cmp', 'dword ptr [ebx + 0x40], eax') in descent_rows and
          ('cmp', 'dword ptr [edi + 0x14], 3') in insert_rows and ('mov', 'eax, 4') in insert_rows and
          ('mov', 'dword ptr [esi + 0x14], 1') in reset_rows and ('mov', 'dword ptr [esi + 0x14], 3') in finish_rows and
          cleanup_rows[-2:] == [('call', hex(BASE + 0x11b0f0)), ('mov', 'dword ptr [ebp + 0x5c], 0')])
    # ---- the AP addresses ----
    ap = C.ap_refusal(im, 'memo', AP_RVAS)
    ap_call = im.read(0x07f329, 5)
    ap_ok = not (ap_call[0] == 0xe8 and (0x07f329 + 5 + int.from_bytes(ap_call[1:], 'little', signed=True)) == QUERY)
    check('ap_addresses_refused', ap['all_refused'] and ap_ok, **ap)
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
