import re,glob,sys,collections
for r in sys.argv[1:]:
    log=glob.glob(f"/tmp/x3-bottleX3-run{r}/session-*.log")[0]
    mx=collections.defaultdict(float); last={}; cur=collections.Counter()
    for l in open(log,errors='replace'):
        if l.startswith('telemetry_metric') and re.search(r'name=\S*(mem|bytes|commit|private|working)',l,re.I):
            d=dict(re.findall(r'(\w+)=(\S+)',l)); n=d.get('name'); v=d.get('value') or d.get('max') or '0'
            try: v=float(v)
            except: continue
            mx[n]=max(mx[n],v); last[n]=v
        elif l.startswith('telemetry_cursor_poll'):
            d=dict(re.findall(r'(\w+)=(\S+)',l)); cur[(d.get('flags'),d.get('cursor'))]+=1
    print('run',r,'mem max',dict(mx),'last',last); print('  cursor (flags,cursor)',dict(cur))
