#!/usr/bin/env python3
"""Raw byte scan of X3AP.exe executable sections for any decodable instruction with a memory
operand displacement 0x12c..0x12f (non-ESP base), independent of Ghidra's code discovery.
Prints candidate VA and text; classifies bit-31 relevance. Usage: raw_disp_scan.py [ghidra_scan.tsv]"""
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__))
import dis_site as d
import capstone
from capstone import x86
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
ghidra = set()
if len(sys.argv) > 1:
    for l in open(sys.argv[1]):
        ghidra.add(int(l.split('\t')[2], 16))
cands = {}
for name, va, vs, ro, rs in d.secs:
    if name not in ('.text',): continue
    blob = d.data[ro:ro + rs]
    for m in re.finditer(rb'[\x2c-\x2f]\x01\x00\x00', blob):
        p = m.start()
        for back in range(1, 4):   # opcode(1-2) + modrm (+sib)
            s = p - back - 1
            for extra in (0, 1):
                st = s - extra
                if st < 0: continue
                for ins in md.disasm(blob[st:st + 15], va + st, count=1):
                    for op in ins.operands:
                        if op.type == x86.X86_OP_MEM and 0x12c <= op.mem.disp <= 0x12f and ins.disp_offset == (p - st):
                            base = ins.reg_name(op.mem.base) if op.mem.base else ''
                            if base == 'esp': continue
                            cands[ins.address] = '%s %s' % (ins.mnemonic, ins.op_str)
n_in = sum(1 for a in cands if a in ghidra)
print('raw candidates (non-esp base):', len(cands), 'at a Ghidra-listed site:', n_in)
for a in sorted(cands):
    t = cands[a]
    bit = ''
    if re.search(r'0x80000000|0x8[0-9a-f]{7}\b|0x[9a-f][0-9a-f]{7}\b', t): bit = 'IMM-HAS-BIT31'
    if re.search(r'\+ 0x12f\]', t) and re.search(r', 0x[89a-f][0-9a-f]$', t): bit = 'BYTE-0x80'
    if t.startswith('bt'): bit = 'BT*'
    if a not in ghidra or bit:
        print('%08x %-9s %-14s %s' % (a, 'ghidra' if a in ghidra else 'NOT-GHIDRA', bit, t))
