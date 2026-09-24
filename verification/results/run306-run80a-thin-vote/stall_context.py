# row kinds logged in the frames just before each stall frame (dt > 1 s)
import re,glob,sys,collections
log=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]
per=set('constant sampler state object_matrix stream transform camera_state texture texture_desc constants vertex_element'.split())
cur=collections.Counter();pe=None;hist=[]
for l in open(log,errors='replace'):
    k=l.split(' ',1)[0]
    if k=='frame_end':
        d=dict(re.findall(r'(\w+)=(\S+)',l)); e=float(d['elapsed_ms'])
        if pe is not None and e-pe>1000: print('frame',d['frame'],'dt_ms',round(e-pe),'kinds',dict((a,b) for a,b in cur.items() if a not in per and b<50))
        pe=e;cur=collections.Counter()
    else: cur[k]+=1
