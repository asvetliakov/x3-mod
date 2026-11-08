"""Independent byte check of src/engine/fl/weapon_effects_sites_fl.h against X3FL.exe (the review of the FL hook port,
docs/verification/weapon-effects.md "FL hook port (2026-11-08)"): every site window and witness masked at its fixup,
the fixup dword = preferred base + target RVA, the .reloc entries inside each window, each call target and its qword
geometry. Read-only; never launches the game.

  python3 -I verification/results/fl-weapon-effect-spawn/fl_windows_check.py src/engine/fl/weapon_effects_sites_fl.h \
    > verification/results/fl-weapon-effect-spawn/fl_windows_check_out.txt
"""
import struct, hashlib, re, sys
p='/mnt/c/X3FL/X3FL.exe'
d=open(p,'rb').read()
print('sha256', hashlib.sha256(d).hexdigest())
pe=struct.unpack_from('<I',d,0x3c)[0]
nsec=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24; optsz=struct.unpack_from('<H',d,pe+20)[0]
base=struct.unpack_from('<I',d,opt+28)[0]
dllch=struct.unpack_from('<H',d,opt+70)[0]
relrva,relsz=struct.unpack_from('<II',d,opt+96+5*8)
secs=[]
for i in range(nsec):
    o=opt+optsz+40*i
    name=d[o:o+8].rstrip(b'\0'); vs,va,rs,rp=struct.unpack_from('<IIII',d,o+8)
    secs.append((name,va,vs,rp,rs))
def r2o(r):
    for n,va,vs,rp,rs in secs:
        if va<=r<va+max(vs,rs): return r-va+rp
    raise Exception(hex(r))
print('base',hex(base),'dllch',hex(dllch),'reloc',hex(relrva),relsz)
relocs=set()
o=r2o(relrva); end=o+relsz
while o<end:
    pg,bs=struct.unpack_from('<II',d,o)
    if bs==0: break
    for k in range((bs-8)//2):
        e=struct.unpack_from('<H',d,o+8+2*k)[0]
        if e>>12==3: relocs.add(pg+(e&0xfff))
        elif e>>12!=0: print('other reloc type',e>>12)
    o+=bs
print('HIGHLOW',len(relocs))
src=open(sys.argv[1]).read()
arr={}
for m in re.finditer(r'constexpr unsigned char (\w+)\[(\d+)\] = \{([^}]*)\}',src):
    arr[m.group(1)]=bytes(int(x,16) for x in re.findall(r'0x[0-9a-f]+',m.group(3)))
    assert len(arr[m.group(1)])==int(m.group(2)),m.group(1)
consts={'gate_slot_rva':0x1a82ec,'type_table_rva':0x1a83c8}
for m in re.finditer(r'\{"(\w+)", (0x[0-9a-f]+), (\d+), (\w+), (\w+), (\w+), (0x[0-9a-f]+)\}(?:, (\d+), (0x[0-9a-f]+))?',src):
    name,rva,ln,b,fo,fr,fnv,co,tgt=m.groups()
    rva=int(rva,16); ln=int(ln)
    live=d[r2o(rva):r2o(rva)+ln]
    rel_in=[x-rva for x in relocs if rva-3<=x<rva+ln]
    exp=arr[b]
    masked=bytearray(live)
    fo_i=None if fo=='no_fixup' else int(fo)
    ok_fix=True
    if fo_i is not None:
        v=struct.unpack_from('<I',live,fo_i)[0]
        ok_fix = v==base+(consts.get(fr) if fr in consts else int(fr,0))
        masked[fo_i:fo_i+4]=b'\0'*4
    extra=''
    if co:
        co=int(co); c=rva+co; rel=struct.unpack_from('<i',live,co+1)[0]
        extra=f' call@{c:#x} tgt={c+5+rel:#x} exp={tgt} q8={"in" if (c&7)+5<=8 else "STRADDLE"}'
    print(f'{name:12s} rva={rva:#08x} len={ln} eq={bytes(masked)==exp} relocs_at={sorted(rel_in)} fixup={fo_i} fixok={ok_fix}{extra}')
