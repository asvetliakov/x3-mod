import math
rows={}
for l in open(__import__('sys').argv[1]):
    if l.startswith('FLOWDUMP'):
        d=dict(kv.split('=') for kv in l.split()[1:])
        rows.setdefault((int(d['w']),int(d['V'])),[]).append((float(d['env']),float(d['p1']),float(d['p2'])))
phase={100:26.2499981,600:21.8749981,1500:8.74999905,10000:7.87499952}
dph={100:0.525,600:0.4375,1500:0.175,10000:0.1575}
for key,r in sorted(rows.items()):
    if key[0]!=1920: continue
    V=key[1]
    a=[x[1]/x[0] for x in r]; b=[x[2]/x[0] for x in r]
    s=dph[V]*60
    res=[]
    for i in range(len(a)):
        j=i-s
        if j<0: continue
        j0=int(math.floor(j)); t=j-j0
        if j0+1>=len(a): continue
        ai=a[j0]*(1-t)+a[j0+1]*t
        res.append((i,b[i]-ai))
    sd=math.sqrt(sum(x*x for _,x in res)/len(res))
    big=[(i,round(x,3)) for i,x in res if abs(x)>4*sd]
    print(key,'rms',round(sd,4),'n_big',len(big))
    for i,x in big[:25]:
        qx=0.8+(i+0.5)/60  # approx, column index -> q.x
        p1=(qx-phase[V]-dph[V])*2.2+5
        print('   i',i,'res',x,'qx~%.3f p_oct1=%.3f oct2=%.3f oct3=%.3f'%(qx,p1,p1*2.03+1.7,p1*4.1+3.1))
