import sys,capstone
d=open('/mnt/c/X3FL/X3FL.exe','rb').read()
a=int(sys.argv[1],16); n=int(sys.argv[2],16)  # RVA
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for i in md.disasm(d[a-0xc00:a-0xc00+n],0x400000+a): print('va=%08x rva=%05x %-20s %s %s'%(i.address,i.address-0x400000,i.bytes.hex(),i.mnemonic,i.op_str))
