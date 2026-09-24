import struct,sys,os
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
P=os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
d=open(P,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]; optsz=struct.unpack_from('<H',d,pe+20)[0]
base=0x400000
secs=[]
for i in range(nsec):
    o=pe+24+optsz+i*40
    name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8)
    secs.append((name,va,vs,ro,rs))
def va2off(a):
    r=a-base
    for n,va,vs,ro,rs in secs:
        if va<=r<va+max(vs,rs): return ro+(r-va)
    return None
IAT={0x532260:'AdjustWindowRectEx',0x532274:'CreateWindowExA',0x532278:'ShowWindow',0x53227c:'GetClientRect',0x532280:'GetSystemMetrics',0x53229c:'GetWindowRect',0x5322a8:'GetCursorPos',0x5322b0:'DefWindowProcA',0x5322bc:'SetCursor',0x5322d4:'SetCursorPos',0x5322dc:'GetMonitorInfoA',0x5322e0:'RegisterClassA',0x5322e8:'SetWindowPos'}
# fill IAT names generically from import table
imp_rva=struct.unpack_from('<I',d,pe+24+0x68)[0]
def rd(a,n): o=va2off(a); return d[o:o+n]
def cstr(a):
    o=va2off(a)
    if o is None: return None
    e=d.index(b'\0',o); s=d[o:e]
    try: return s.decode('ascii') if all(32<=c<127 for c in s) and len(s)>=2 else None
    except: return None
o=va2off(base+imp_rva)
while True:
    ilt,ts,fc,nm,iat=struct.unpack_from('<IIIII',d,o)
    if not nm: break
    dll=cstr(base+nm); thunk=ilt or iat; k=0
    while True:
        e=struct.unpack_from('<I',rd(base+thunk+k*4,4),0)[0]
        if not e: break
        fn=('ord%d'%(e&0xffff)) if e&0x80000000 else cstr(base+e+2)
        IAT[base+iat+k*4]=fn; k+=1
    o+=20
md=Cs(CS_ARCH_X86,CS_MODE_32); md.detail=False
start=int(sys.argv[1],16); end=int(sys.argv[2],16)
code=d[va2off(start):va2off(start)+(end-start)]
for ins in md.disasm(code,start):
    s='%08x %-6s %s'%(ins.address,ins.mnemonic,ins.op_str)
    import re
    m=re.search(r'\[0x([0-9a-f]+)\]',ins.op_str)
    if m and int(m.group(1),16) in IAT: s+='   ; '+IAT[int(m.group(1),16)]
    m2=re.search(r'0x([0-9a-f]{6})\b',ins.op_str)
    if m2 and ins.mnemonic=='push':
        v=int(m2.group(1),16); t=cstr(v)
        if t: s+='   ; "%s"'%t
    print(s)
