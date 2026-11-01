import struct,sys
class MD:
    def __init__(s,p):
        s.b=open(p,'rb').read(); b=s.b
        sig,ver,n,rva=struct.unpack_from('<4sIII',b,0); assert sig==b'MDMP'
        s.streams={}
        for i in range(n):
            t,sz,r=struct.unpack_from('<III',b,rva+12*i); s.streams[t]=(sz,r)
        s.mem=[]
        if 5 in s.streams:
            sz,r=s.streams[5]; c=struct.unpack_from('<I',b,r)[0]
            for i in range(c):
                st,ds,dr=struct.unpack_from('<QII',b,r+4+16*i); s.mem.append((st,ds,dr))
        if 9 in s.streams:
            sz,r=s.streams[9]; c,base=struct.unpack_from('<QQ',b,r); off=base
            for i in range(c):
                st,ds=struct.unpack_from('<QQ',b,r+16+16*i); s.mem.append((st,ds,off)); off+=ds
        s.mods=[]
        if 4 in s.streams:
            sz,r=s.streams[4]; c=struct.unpack_from('<I',b,r)[0]
            for i in range(c):
                o=r+4+108*i; base,size,cs,ts,nr=struct.unpack_from('<QIIII',b,o)
                ln=struct.unpack_from('<I',b,nr)[0]; name=b[nr+4:nr+4+ln].decode('utf-16le')
                s.mods.append((base,size,name,ts))
    def read(s,a,n):
        for st,ds,o in s.mem:
            if st<=a and a+n<=st+ds: return s.b[o+a-st:o+a-st+n]
        return None
    def modof(s,a):
        for base,size,name,ts in s.mods:
            if base<=a<base+size: return (name.split('\\')[-1],base)
        return None
