# Q3 (run332 follow-up): pixels that a far band would touch, per frame, from depth_1 (lane .r valid in [0,1], .b view z):
# farw > 0 needs z > f0 * 1280 (p00 0.5, width 5120), farw = 1 needs z >= f1 * 1280. Counts for the shipped 80/130 and for
# candidate bands; with the taa_age dump (AGEDIR) also the share outside the region (h == 0), the pixels that would take a
# rank-1 7x7 clip. Frame share of 5120 x 1440 = 7,372,800 px.
# usage: far_px_counts.py RUN F [F ...]  [env AGEDIR, BANDS="80,130 60,68 50,68"]
import sys,os,numpy as np
RUN=sys.argv[1]; FR=sys.argv[2:]; TD=f'/tmp/x3-bottleX3-run{RUN}'; AD=os.environ.get('AGEDIR')
BANDS=[tuple(map(float,b.split(','))) for b in os.environ.get('BANDS','80,130 60,68 50,68').split()]; S=0.4999979*5120/2; TOT=5120*1440
for f in FR:
    a=np.memmap(f'{TD}/depth_1_{f}.rgba32f',np.float32,'r',shape=(1440,5120,4)); r=np.array(a[...,0]); z=np.array(a[...,2]); ok=(r>=0)&(r<=1)
    reg=None
    if AD and os.path.exists(f'{AD}/taa_age_1_{f}.r32f'):
        v=np.abs(np.fromfile(f'{AD}/taa_age_1_{f}.r32f',np.float32).reshape(1440,5120)); reg=(np.where(v<=65,np.round((v-np.floor(v))*65536),0).astype(np.int64)%128)>0
    out=[f'run{RUN} f{f} valid-depth px {int(ok.sum())}']
    for f0,f1 in BANDS:
        gt0=ok&(z>f0*S); one=ok&(z>=f1*S)
        s=f' | {f0:g}/{f1:g} ({f0*S/1e3:.1f}-{f1*S/1e3:.1f} km): farw>0 {int(gt0.sum())} ({100*gt0.sum()/TOT:.2f}%) farw=1 {int(one.sum())}'
        if reg is not None: s+=f' farw>0&h=0 {int((gt0&~reg).sum())} ({100*(gt0&~reg).sum()/TOT:.2f}%)'
        out.append(s)
    zz=z[ok]; out.append(f' | view z of valid px 20-64 km {int(((zz>20000)&(zz<=64000)).sum())} 64-102.4 km {int(((zz>64000)&(zz<=102400)).sum())}')
    print(''.join(out))
