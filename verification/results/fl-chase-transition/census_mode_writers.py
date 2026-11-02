"""usage: python3 -I verification/results/fl-chase-transition/census_mode_writers.py > census_mode_writers.txt
Census of instructions in X3FL.exe .text that write a register-relative dword at displacement +0x150."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'probe'))
from pathlib import Path
import capstone
from capstone import x86
from verify_chase_camera_site import FlImage
img = FlImage(Path('/mnt/c/X3FL/X3FL.exe').read_bytes())
_, start, _, rp, rsize = next(s for s in img.sections if s[0] == '.text')
code = img.data[rp:rp + rsize]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
hits, pos = [], 0
while pos < len(code):
    last = pos
    for i in md.disasm(code[pos:pos + 0x10000], start + pos):
        last = i.address + i.size - start
        if not i.operands:
            continue
        op = i.operands[0]
        if op.type == x86.X86_OP_MEM and op.mem.disp == 0x150 and op.mem.base != 0 and op.size == 4 and \
                i.mnemonic not in ('cmp', 'test', 'push') and (op.access & capstone.CS_AC_WRITE):
            hits.append(f'{i.address - 0x400000:#08x} {i.mnemonic} {i.op_str}')
    pos = last if last > pos else pos + 1
for h in hits:
    print(h)
print('count', len(hits))
