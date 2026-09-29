# Resolve X3AP.exe IAT slots / call-site targets used by the run379 frame table.
import struct,os,sys
p=os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
b=open(p,'rb').read()
pe=struct.unpack_from('<I',b,0x3c)[0]; nsec=struct.unpack_from('<H',b,pe+6)[0]; opt=struct.unpack_from('<H',b,pe+20)[0]
secs=[struct.unpack_from('<8sIIII',b,pe+24+opt+40*i) for i in range(nsec)]
def off(va):
    rva=va-0x400000
    for n,vs,va_,rs,ro in secs:
        if va_<=rva<va_+max(vs,rs): return ro+rva-va_
imp_rva=struct.unpack_from('<I',b,pe+24+104)[0]
o=off(0x400000+imp_rva); names={}
while True:
    oft,ts,fc,nm,ft=struct.unpack_from('<5I',b,o); o+=20
    if nm==0: break
    dll=b[off(0x400000+nm):].split(b'\0')[0].decode()
    t=off(0x400000+(oft or ft)); i=0
    while True:
        e=struct.unpack_from('<I',b,t+4*i)[0]
        if e==0: break
        fn=b[off(0x400000+e)+2:].split(b'\0')[0].decode() if not e&0x80000000 else f'ord{e&0xffff}'
        names[0x400000+ft+4*i]=dll+'!'+fn; i+=1
for a in sys.argv[1:]:
    print(a,names.get(int(a,16)))
