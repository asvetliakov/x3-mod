import sys,struct; sys.path.insert(0,sys.argv[2]); from md import MD
m=MD(sys.argv[1]); b=m.b
sz,r=m.streams[3]; c=struct.unpack_from('<I',b,r)[0]
for i in range(c):
    o=r+4+48*i; tid,sc,pc,pr,teb,stk,ds,dr,cs,cr=struct.unpack_from('<IIIIQQIIII',b,o)
    if tid==29016: print('thread stack %x..%x'%(stk,stk+ds)); lo,hi=stk,stk+ds
esp=0x16ff9f8
d=m.read(esp,hi-esp)
exe=None
for k in range(0,len(d),4):
    v=struct.unpack_from('<I',d,k)[0]; mo=m.modof(v)
    if mo and mo[0].lower() in ('x3fl.exe','d3d9.dll') and (mo[0]!='d3d9.dll' or mo[1]==0x6d270000):
        # check preceding call
        pre=m.read(v-7,7) ; call=''
        if pre:
            if pre[2]==0xe8: call='E8 ->%x'%((v+struct.unpack_from('<i',pre,3)[0])&0xffffffff)
            elif pre[5]==0xff or pre[4]==0xff or pre[1]==0xff: call='FF call'
        else: call='code-not-in-dump'
        print('%08x +%04x %08x %s rva=%x %s'%(esp+k,k,v,mo[0],v-mo[1],call))
