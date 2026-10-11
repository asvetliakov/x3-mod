# usage: python3 -I field_writers.py <exe> 0xOFF...  - linear-sweep the first code section and print every
# instruction whose destination memory operand has displacement OFF (write candidates: mov/or/and/inc/dec/xor),
# with the enclosing function guess = nearest preceding 'push ebp; mov ebp,esp' or int3/ret-aligned start is NOT
# attempted; addresses only. Linear sweep can desynchronise; treat hits as candidates.
import sys, struct, capstone
exe = sys.argv[1]; offs = [int(x, 16) for x in sys.argv[2:]]
d = open(exe, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
o = pe + 24 + opt
vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
from capstone import x86
W = {'mov', 'or', 'and', 'xor', 'inc', 'dec', 'add', 'sub', 'movb', 'movw'}
code = d[ro:ro + rs]; pc = 0
while pc < len(code):
    got = False
    for ins in md.disasm(code[pc:pc + 4096], base + va + pc):
        got = True; pc = ins.address - base - va + ins.size
        if ins.mnemonic in W and ins.operands and ins.operands[0].type == x86.X86_OP_MEM and ins.operands[0].mem.disp in offs:
            print(f"{ins.address:08x} {ins.mnemonic} {ins.op_str}")
        if pc >= len(code): break
    if not got: pc += 1
