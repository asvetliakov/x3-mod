import re,glob,sys
log=glob.glob(f"/tmp/x3-bottleX3-run{sys.argv[1]}/session-*.log")[0]
last={}
for l in open(log,errors='replace'):
    if l.startswith('gpu_sync_timing window='):
        d=dict(re.findall(r'(\w+)=(\S+)',l)); last[d['pass']]=d
for p,d in sorted(last.items(),key=lambda x:-int(x[1]['session_median_us'])):
    print(p,'session_median_us',d['session_median_us'],'p90',d['session_p90_us'],'n',d['session_n'],'dt_median_us',d['dt_median_us'])
