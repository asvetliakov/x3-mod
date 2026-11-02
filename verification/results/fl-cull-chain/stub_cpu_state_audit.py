#!/usr/bin/env python3
"""FL small-parts stub CPU-state audit (src/engine/fl/cull_small_parts_sites_fl.h encode_stub; docs/verification/
cull-small-parts.md, "FL port").

Reads the STUB lines the native fixture verification/probe/fl_cull_chain.cpp prints (the bytes the production module
emitted into the engine_patch arena, with the counter, cull-target and far-handler addresses) and walks every reachable
instruction of each variant from the entry with Capstone, tracking the stack symbolically. Requires: no x87/MMX/SSE/AVX
instruction or register; no write to EBX, ESI, EDI or EBP; ECX written only by the `pop ecx` that restores its own
`push ecx`; memory written only by `inc dword [counter]` and pushes; [ESP+n] read only as s (site [ESP+0x10]); the one
call (far variants only) to the far handler with (node ESI, measure ECX, view EBX) pushed and ECX saved; every exit with
the site's ESP, either `jmp [slot]` (the tail) or `jmp rel32` to base+0x0961f3 (the engine's cull); no int3 reachable.
Hence LastError (no Win32 call in the stub), XMM, x87, MXCSR and DF are untouched by the stub itself.

usage: stub_cpu_state_audit.py <fixture stdout file>   (exit 1 on a finding; prints one JSON line)
"""
import json
import re
import sys

import capstone
from capstone import x86

LINE = re.compile(r'^STUB variant=(\S+) at=([0-9a-f]{8}) cull=([0-9a-f]{8}) upper=([0-9a-f]{8}) threshold=([0-9a-f]{8}) '
                  r'culled=([0-9a-f]{8}) exempt=([0-9a-f]{8}) dock=([0-9a-f]{8}) far=([0-9a-f]{8}) far_on=([01]) '
                  r'hex=([0-9a-f]+)$')
FORBIDDEN_GROUPS = {x86.X86_GRP_FPU, x86.X86_GRP_MMX, x86.X86_GRP_SSE1, x86.X86_GRP_SSE2, x86.X86_GRP_SSE3,
                    x86.X86_GRP_SSSE3, x86.X86_GRP_SSE41, x86.X86_GRP_SSE42, x86.X86_GRP_AVX, x86.X86_GRP_AVX2}
S_SLOT = 0x10


def audit(variant, at, cull, counters, reads, far, far_on, code):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    findings, seen, exits, calls = [], set(), {'continue': 0, 'cull': 0}, 0
    reached = set()
    work = [(0, ())]   # (offset, symbolic stack: pushed register names, innermost last)
    while work:
        off, stack = work.pop()
        if (off, stack) in seen:
            continue
        seen.add((off, stack))
        if not 0 <= off < len(code):
            findings.append(f'{variant}: flow leaves the stub at +{off}')
            continue
        insn = next(md.disasm(code[off:], at + off, 1), None)
        if insn is None:
            findings.append(f'{variant}: undecodable at +{off}')
            continue
        reached.add(off)
        text = f'+{off} {insn.mnemonic} {insn.op_str}'
        nxt = off + insn.size
        if set(insn.groups) & FORBIDDEN_GROUPS or re.search(r'\b(?:st\(|x?mm\d|ymm\d)', insn.op_str):
            findings.append(f'{variant}: floating/SIMD {text}')
        if insn.mnemonic == 'int3':
            findings.append(f'{variant}: int3 reachable {text}')
            continue
        _, written = insn.regs_access()
        names = {insn.reg_name(r) for r in written}
        if names & {'ebx', 'esi', 'edi', 'ebp', 'bx', 'si', 'di', 'bp', 'bl', 'bh'}:
            findings.append(f'{variant}: callee-saved register written {text}')
        if names & {'ecx', 'cx', 'cl', 'ch'} and not (insn.mnemonic == 'pop' and insn.op_str == 'ecx'):
            findings.append(f'{variant}: ECX written {text}')
        for op in insn.operands:
            if op.type != x86.X86_OP_MEM:
                continue
            base = insn.reg_name(op.mem.base) if op.mem.base else None
            if op.access & capstone.CS_AC_WRITE and insn.mnemonic not in ('push', 'call'):
                if not (insn.mnemonic == 'inc' and base is None and (op.mem.disp & 0xffffffff) in counters):
                    findings.append(f'{variant}: memory write {text}')
            if base == 'esp' and insn.mnemonic != 'push':
                slot = op.mem.disp - 4 * len(stack)
                if slot != S_SLOT:
                    findings.append(f'{variant}: stack slot {slot:#x} read {text}')
            if base is None and insn.mnemonic in ('mov', 'cmp') and (op.mem.disp & 0xffffffff) not in reads:
                findings.append(f'{variant}: absolute read outside the thresholds {text}')
        m = insn.mnemonic
        if m == 'push':
            work.append((nxt, stack + (insn.op_str,)))
        elif m == 'pop':
            if not stack or stack[-1] != insn.op_str:
                findings.append(f'{variant}: pop of {insn.op_str} over {stack[-1:] or "nothing"} {text}')
                continue
            work.append((nxt, stack[:-1]))
        elif m == 'add' and insn.op_str.startswith('esp,'):
            n = int(insn.op_str.split(',')[1], 0) // 4
            if n > len(stack):
                findings.append(f'{variant}: add esp below the site {text}')
                continue
            work.append((nxt, stack[:-n] if n else stack))
        elif m == 'call':
            calls += 1
            target = insn.operands[0].imm & 0xffffffff
            if not far_on or target != far:
                findings.append(f'{variant}: call {target:#x} (far_on={far_on}) {text}')
            if stack != ('ecx', 'ebx', 'ecx', 'esi'):
                findings.append(f'{variant}: call frame {stack} (want ecx saved, then view ebx, measure ecx, node esi)')
            work.append((nxt, stack))
        elif m == 'jmp':
            op = insn.operands[0]
            if op.type == x86.X86_OP_MEM:
                if stack:
                    findings.append(f'{variant}: continue with {len(stack)} pushes {text}')
                exits['continue'] += 1
            elif op.type == x86.X86_OP_IMM and at <= (op.imm & 0xffffffff) < at + len(code):
                work.append(((op.imm & 0xffffffff) - at, stack))
            elif op.type == x86.X86_OP_IMM and (op.imm & 0xffffffff) == cull:
                if stack:
                    findings.append(f'{variant}: cull with {len(stack)} pushes {text}')
                exits['cull'] += 1
            else:
                findings.append(f'{variant}: jump elsewhere {text}')
        elif m.startswith('j'):
            target = insn.operands[0].imm & 0xffffffff
            if not at <= target < at + len(code):
                findings.append(f'{variant}: branch out of the stub {text}')
            else:
                work.append((target - at, stack))
            work.append((nxt, stack))
        elif m in ('ret', 'retf', 'iret', 'int', 'syscall', 'sysenter', 'hlt'):
            findings.append(f'{variant}: {m} in the stub {text}')
        else:
            work.append((nxt, stack))
    return dict(instructions=len(reached), calls=calls, exits=exits, findings=len(findings)), findings


def main():
    variants, findings = {}, []
    for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
        m = LINE.match(line.strip())
        if not m:
            continue
        name, at, cull, upper, threshold, culled, exempt, dock, far, far_on, code = m.groups()
        info, found = audit(name, int(at, 16), int(cull, 16), {int(culled, 16), int(exempt, 16), int(dock, 16)},
                            {int(upper, 16), int(threshold, 16)}, int(far, 16), far_on == '1', bytes.fromhex(code))
        variants[name] = info
        findings += found
    print(json.dumps(dict(variants=variants, findings=findings)))
    return 1 if findings or set(variants) != {'full', 'no_projectiles', 'no_far'} else 0


if __name__ == '__main__':
    sys.exit(main())
