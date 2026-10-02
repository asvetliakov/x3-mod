import glob,collections as C
L=glob.glob('/tmp/x3-bottleX3-run403/session-*.log')[0]
n=z=a0=st=0; tr=0; prev=None
for l in open(L,errors='replace'):
    if not l.startswith('engine_stage '): continue
    d=dict(t.split('=',1) for t in l.split()[1:] if '=' in t)
    if d['armed']!='1' or d['nozzles']=='0': continue
    n+=1; z+= d['ribbons']=='0' and d['ribbons_live']!='0'; a0+= d['ribbon_appended']=='0'
print('armed frames with nozzles',n,'ribbons drawn 0 while live>0',z,'ribbon_appended==0',a0)
