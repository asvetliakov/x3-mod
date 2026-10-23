# frame_end dt vs draws per 100-frame bucket in flight (frames 1490-4470), Run193 x3m.log
import re,sys,statistics as st
R=[]
for l in open(sys.argv[1],'rb'):
    if l.startswith(b'frame_end '):
        d=dict(re.findall(r'(\w+)=(\S+)',l.decode('latin1')))
        f=int(d['frame'])
        if 1490<=f<=4470: R.append((f,int(d['dt_ms']),int(d['draws'])))
for i in range(0,len(R),150):
    b=R[i:i+150]; print(b[0][0], 'dt_p50',st.median(x[1] for x in b),'draws_p50',st.median(x[2] for x in b),'draws_min',min(x[2] for x in b),'draws_max',max(x[2] for x in b))
print('corr dt~draws flight', round(st.correlation([x[1] for x in R],[x[2] for x in R]),3))
