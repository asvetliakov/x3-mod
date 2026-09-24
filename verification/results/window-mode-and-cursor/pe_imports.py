import struct,sys,os
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
P=os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
d=open(P,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]; optsz=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<I',d,pe+0x34)[0]
secs=[]
for i in range(nsec):
    o=pe+24+optsz+i*40
    name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8)
    secs.append((name,va,vs,ro,rs))
def rva2off(r):
    for n,va,vs,ro,rs in secs:
        if va<=r<va+max(vs,rs): return ro+(r-va)
    raise ValueError(hex(r))
def rd(r,n): o=rva2off(r); return d[o:o+n]
def cstr(r):
    o=rva2off(r); e=d.index(b'\0',o); return d[o:e].decode(errors='replace')
imp_rva=struct.unpack_from('<I',d,pe+24+0x68)[0]
imports={}
o=rva2off(imp_rva)
while True:
    ilt,ts,fc,nm,iat=struct.unpack_from('<IIIII',d,o)
    if not nm: break
    dll=cstr(nm); thunk=ilt or iat; k=0
    while True:
        e=struct.unpack_from('<I',rd(thunk+k*4,4),0)[0]
        if not e: break
        fn=('ord%d'%(e&0xffff)) if e&0x80000000 else cstr(e+2)
        imports[base+iat+k*4]=(dll,fn); k+=1
    o+=20
print('sections',[(n,hex(base+va),hex(vs)) for n,va,vs,ro,rs in secs])
want=sys.argv[1:]
sel={a:(dll,fn) for a,(dll,fn) in imports.items() if fn in want or (not want and dll.lower()=='user32.dll')}
for a,(dll,fn) in sorted(sel.items()): print('import',hex(a),dll,fn)
# scan .text for FF 15 <iat>
tn,tva,tvs,tro,trs=[s for s in secs if s[0]=='.text'][0]
text=d[tro:tro+trs]
sites={}
for a,(dll,fn) in sel.items():
    pat=b'\xff\x15'+struct.pack('<I',a); i=0
    while True:
        i=text.find(pat,i)
        if i<0: break
        sites.setdefault(fn,[]).append(base+tva+i); i+=1
    pat=b'\xff\x25'+struct.pack('<I',a); i=text.find(pat)
    if i>=0: sites.setdefault(fn,[]).append(('jmp',base+tva+i))
for fn in sorted(sites): print('sites',fn,[hex(x) if isinstance(x,int) else x for x in sites[fn]])
