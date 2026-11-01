import sys,struct,re
d=open('/mnt/c/X3FL/X3FL.exe','rb').read()
for line in open(sys.argv[1]):
    mm=re.search(r'^(\w+) \+(\w+) (\w+) X3FL.exe rva=(\w+)',line)
    if not mm: continue
    rva=int(mm.group(4),16); o=rva-0xc00
    if o<0x400: print(line.strip()[:40],'not text');continue
    p=d[o-7:o]; s='no-call'
    if p[2]==0xe8: s='call E8 -> rva %x'%((rva+struct.unpack_from('<i',p,3)[0])&0xffffffff)
    elif p[5]==0xff and (p[6]>>3)&7==2: s='call ff /2 reg'
    elif p[4]==0xff and (p[5]>>3)&7==2: s='call ff /2 [r+d8] %s'%p[4:].hex()
    elif p[1]==0xff and (p[2]>>3)&7==2: s='call ff /2 m32 %s'%p[1:].hex()
    elif p[3]==0xff and (p[4]>>3)&7==2: s='call ff /2 %s'%p[3:].hex()
    print(mm.group(1),'+'+mm.group(2),'rva=%x'%rva,s)
