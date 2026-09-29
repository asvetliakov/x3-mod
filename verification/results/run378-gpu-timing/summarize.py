import re,sys,glob
def kv(l): return dict(re.findall(r'(\w+)=(\S+)',l))
for run in ('run378','run376'):
    f=glob.glob(f'/tmp/x3-bottleX3-{run}/session-*.log')[0]
    print('==',run)
    for l in open(f,errors='replace'):
        if l.startswith('gpu_sync_timing window='):
            d=kv(l);print('gpu',d['window'],d['pass'],'med',d['median_us'],'p90',d['p90_us'],'n',d['n'],'wait',d.get('wait_median_us'),'dt',d['dt_median_us'],'frames',d['frames'])
        elif l.startswith('frame_phases '):
            d=kv(l);print('ph',d['frame'],'dt',d['dt_p50_us'],d['dt_p95_us'],'pre',d['pre_render_p50_us'],d['pre_render_p95_us'],'views',d['views_p50_us'],d['views_p95_us'],'se',d['scene_end_p50_us'],d['scene_end_p95_us'],'pres',d['present_p50_us'],d['present_p95_us'])
        elif l.startswith('frame_end') and 'draws_p50' in l:
            d=kv(l);print('fe',d.get('frame'),'draws_p50',d.get('draws_p50'),'issued',d.get('issued_p50',d.get('issued')))
# draw count and dt per 300-frame window from frame_end rows
for run in ('run378','run376'):
    f=glob.glob(f'/tmp/x3-bottleX3-{run}/session-*.log')[0]
    rows=[(int(m.group(1)),int(m.group(2))) for l in open(f,errors='replace') if (m:=re.match(r'frame_end device=\d+ frame=\d+ draws=(\d+).*dt_ms=(\d+)',l))]
    for a in range(0,len(rows),300):
        d=sorted(x[0] for x in rows[a:a+300]); t=sorted(x[1] for x in rows[a:a+300])
        print('fe',run,a,'draws_p50',d[len(d)//2],'dt_ms_p50',t[len(t)//2])
