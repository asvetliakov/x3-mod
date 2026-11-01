# usage: python3 lens_pos.py <frame-slice.log> first last
# Projects object origin through VS c0..c3 (row-major clip rows) per draw; prints screen centre and quad half-size from c0.x/c1.y
import sys, struct
f=lambda h: struct.unpack('>f',bytes.fromhex(h))[0]
a,b=int(sys.argv[2]),int(sys.argv[3]);idx=None;c={}
W,H=5120,1440
def out():
    if idx is None or not (a<=idx<=b) or 3 not in c: return
    w=c[3][3]
    if w==0: return
    x=c[0][3]/w; y=c[1][3]/w
    print(idx,'screen=(%.0f,%.0f)'%((x+1)/2*W,(1-y)/2*H),'half_px=(%.0f,%.0f)'%(c[0][0]/w*W/2,c[1][1]/w*H/2),'w=%.2f'%w)
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('draw '):
        out(); idx=int(l.split('index=')[1].split()[0]); c={}
    elif l.startswith('constant kind=vs type=f reg=') :
        r=int(l.split('reg=')[1].split()[0])
        if r<4: c[r]=[f(h) for h in l.split('bits=')[1].strip().split(',')]
out()
