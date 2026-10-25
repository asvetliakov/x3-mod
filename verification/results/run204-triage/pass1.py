# one pass over x3m.log: frame dt, lod_strength_frame field changes, row kinds per frame
import sys,re,collections,json
dt={};kinds=collections.Counter();lsf=0;prev=None;chg=collections.Counter();lsfframes=set();perframe=collections.defaultdict(collections.Counter)
cur=0;last_lsf=None
for line in open(sys.argv[1],errors='replace'):
    k=line.split(' ',1)[0];kinds[k]+=1
    if k=='frame_end':
        m=re.search(r' frame=(\d+).*dt_ms=(\d+)',line)
        if m: cur=int(m.group(1)); dt[cur]=int(m.group(2))
        continue
    if k not in('draw','draw_detail'): perframe[cur+1][k]+=1
    if k=='lod_strength_frame':
        d=dict(x.split('=',1) for x in line.split()[1:]);lsfframes.add(int(d['frame']))
        if last_lsf:
            for f in d:
                if f!='frame' and d[f]!=last_lsf.get(f): chg[f]+=1
        last_lsf=d
json.dump({'dt':dt,'perframe':{f:dict(c) for f,c in perframe.items()},'kinds':kinds.most_common(80),'chg':chg,'lsf':len(lsfframes),'last_lsf':last_lsf},open(sys.argv[2],'w'))
