"""Stack-argument scan of X3FL.exe's renderer-load target [RVA 0x93510, 0x94170) (reviewer's script, step 3b).

Lists every operand at [ebp+5..+0x3f] (a caller stack word once `push ebp; mov ebp,esp` has run) and every [esp+..]
operand in the first 0x20 bytes. The run (load_target_stack_scan.txt): 990 instructions, two hits, both locals
addressed after `and esp,-16; sub esp,0x124` (0x493523, 0x49352f): the target reads none of its stack arguments,
so the dispatcher's one forwarded word is never read. usage: load_target_stack_scan.py
"""
import sys, capstone, struct
data=open('/mnt/c/X3FL/X3FL.exe','rb').read()
pe=struct.unpack_from('<I',data,0x3c)[0]; nsec=struct.unpack_from('<H',data,pe+6)[0]; optsz=struct.unpack_from('<H',data,pe+20)[0]
secs=[struct.unpack_from('<8sIIII',data,pe+24+optsz+i*40) for i in range(nsec)]
def off(rva):
    for n,vs,va,rs,ro in secs:
        if va<=rva<va+max(vs,rs): return ro+rva-va
lo,hi=0x93510,0x94170
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); md.detail=True
code=data[off(lo):off(hi)]
hits=[]; n=0
for i in md.disasm(code,0x400000+lo):
    n+=1
    for op in i.operands:
        if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_EBP and 4<op.mem.disp<0x40:
            hits.append(f'{i.address:x} {i.mnemonic} {i.op_str}')
        if op.type==capstone.x86.X86_OP_MEM and op.mem.base==capstone.x86.X86_REG_ESP and i.address<0x400000+lo+0x20:
            hits.append(f'{i.address:x} {i.mnemonic} {i.op_str}')
print('insns',n,'positive-ebp/early-esp refs',len(hits)); print('\n'.join(hits[:12]))
