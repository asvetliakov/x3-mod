import sys,struct; sys.path.insert(0,sys.argv[2]); from md import MD
m=MD(sys.argv[1]); b=m.b
print('streams',sorted(m.streams)); print('memranges',len(m.mem))
for base,size,name,ts in m.mods:
    n=name.split('\\')[-1]
    if n.lower() in('x3fl.exe','d3d9.dll','ntdll.dll','kernelbase.dll','kernel32.dll') or 'd3d9' in n.lower(): print('mod %08x %08x %s %s'%(base,size,name,ts))
sz,r=m.streams[6]
tid,_=struct.unpack_from('<II',b,r); code,flags,rec,addr,np=struct.unpack_from('<IIQQI',b,r+8)
print('exc tid',tid,'code %08x addr %x'%(code,addr))
csz,crva=struct.unpack_from('<II',b,r+8+152)
ctx=b[crva:crva+csz]
# x86 CONTEXT: flags@0, Dr0-7 @4..28, FloatSave 112 @28, Gs@140 Fs Es Ds Edi@156 Esi Ebx Edx Ecx Eax Ebp Eip SegCs EFlags Esp SegSs
names=['Gs','Fs','Es','Ds','Edi','Esi','Ebx','Edx','Ecx','Eax','Ebp','Eip','SegCs','EFlags','Esp','SegSs']
v=struct.unpack_from('<16I',ctx,140); R=dict(zip(names,v))
print('ctxflags %08x size %d'%(struct.unpack_from('<I',ctx,0)[0],csz))
print(' '.join('%s=%08x'%(k,R[k]) for k in names[4:]))
# ExtendedRegisters at 204, XMM at +160
if csz>=716:
    for i in range(8): print('xmm%d'%i, ctx[204+160+16*i:204+176+16*i].hex())
esp=R['Esp']
st=m.read(esp,0x40); print('stack args', [hex(x) for x in struct.unpack('<16I',st)])
open(sys.argv[2]+'/regs.txt','w').write(repr(R))
