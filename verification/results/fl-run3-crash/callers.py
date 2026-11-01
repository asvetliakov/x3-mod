# E8 rel32 callers of a target RVA in X3FL.exe (file offset = RVA - 0xc00). Usage: python3 -I callers.py 12d10
import struct,sys
d=open('/mnt/c/X3FL/X3FL.exe','rb').read();t=int(sys.argv[1],16)
print([hex(0x400000+o+0xc00) for o in range(0x400,len(d)-5) if d[o]==0xe8 and o+0xc00+5+struct.unpack_from('<i',d,o+1)[0]==t])
