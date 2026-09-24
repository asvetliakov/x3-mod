import struct,sys,os
P=os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe')
d=open(P,'rb').read(); base=0x400000
text_off=0x1000-0x1000+0x400  # computed below
pe=struct.unpack_from('<I',d,0x3c)[0]; nsec=struct.unpack_from('<H',d,pe+6)[0]; optsz=struct.unpack_from('<H',d,pe+20)[0]
secs={}
for i in range(nsec):
    o=pe+24+optsz+i*40; name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8); secs[name]=(va,vs,ro,rs)
tva,tvs,tro,trs=secs['.text']; text=d[tro:tro+trs]
for arg in sys.argv[1:]:
    if arg.startswith('str:'):
        s=arg[4:].encode()+b'\0'
        for nm,(va,vs,ro,rs) in secs.items():
            i=d.find(s,ro,ro+rs)
            while i>=0 and i<ro+rs:
                a=base+va+(i-ro); 
                # only whole-string start (preceded by NUL)
                if d[i-1]==0:
                    hits=[hex(base+tva+j) for j in range(len(text)-4) if text[j:j+4]==struct.pack('<I',a)]
                    print('string',arg[4:],'at',hex(a),'refs',hits[:20])
                i=d.find(s,i+1,ro+rs)
    else:
        t=int(arg,16); hits=[]
        for j in range(len(text)-5):
            if text[j]==0xe8 and struct.unpack_from('<i',text,j+1)[0]+base+tva+j+5==t: hits.append(hex(base+tva+j))
        print('calls to',arg,hits[:30])
