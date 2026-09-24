"""Per 300-frame window (frame//300): frame_end dt p10/p50/p90 (ms), quantisation share (dt in 16-17 / 33-34 ms bins),
draws p50, motion_output routed p50, camera rest/pan (camera_rotation_deg p90 / frac>0.05), thin_vote draw_us+lock_us p50,
fog sector index/profile, and frame_phases p50 (pre_render, views, view_setup, view_submit, scene_end, present).
usage: frame_windows.py RUN..."""
import glob, re, sys, collections, statistics as st
KV = re.compile(r'(\w+)=(\S+)')
def q(v, p): v = sorted(v); return v[int(p * (len(v) - 1))] if v else float('nan')
for run in sys.argv[1:]:
    log = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))[0]
    dt = collections.defaultdict(list); dr = collections.defaultdict(list); ro = collections.defaultdict(list)
    rot = collections.defaultdict(list); tv = collections.defaultdict(list); ph = {}; sect = []; gs = 0
    for l in open(log, errors='replace'):
        h = l[:24]
        if h.startswith('frame_end '):
            d = dict(KV.findall(l)); f = int(d['frame']); k = f // 300
            if f > 0: dt[k].append(int(d['dt_ms'])); dr[k].append(int(d['draws']))
        elif h.startswith('motion_output_frame '):
            d = dict(KV.findall(l)); k = int(d['frame']) // 300; ro[k].append(int(d['routed']))
            if 'camera_rotation_deg' in d: rot[k].append(float(d['camera_rotation_deg']))
        elif h.startswith('thin_vote_frame '):
            d = dict(KV.findall(l)); k = int(d['frame']) // 300; tv[k].append(float(d['draw_us']) + float(d.get('lock_us', 0)))
        elif h.startswith('frame_phases '):
            d = dict(KV.findall(l)); ph[int(d['frame']) // 300 - 1] = d
        elif h.startswith('volumetric_fog_sector'):
            d = dict(KV.findall(l)); sect.append((int(d['frame']), d['index'], d['reason']))
        elif h.startswith('gpu_sync_timing_mode'): gs = 1
    print(f'run{run} gpu_sync_timing={gs} sectors={sect}')
    print('  win  n   dt10 dt50 dt90 q16-17 q33-34 draws routed rotp90 frac>.05 cam  thin_us | pre views vsetup vsubmit send present (p50 ms)')
    for k in sorted(dt):
        v = dt[k]; sidx = [s for s in sect if s[0] <= k * 300][-1:] or [(0, '?', '?')]
        r = rot[k]; fr = sum(x > 0.05 for x in r) / len(r) if r else 0; p90r = q(r, .9)
        cam = 'rest' if p90r < 0.02 else 'pan' if fr >= 0.5 else 'mixed'
        p = ph.get(k); pf = (lambda n: f"{int(p[n+'_p50_us'])/1000:5.1f}") if p else (lambda n: '    -')
        print(f"  w{k:<3} {len(v):3d} {q(v,.1):4d} {q(v,.5):4d} {q(v,.9):4d} {sum(16<=x<=17 for x in v)/len(v):6.2f} {sum(33<=x<=34 for x in v)/len(v):6.2f}"
              f" {st.median(dr[k]):5.0f} {st.median(ro[k]) if ro[k] else 0:6.0f} {p90r:6.3f} {fr:8.2f} {cam:5s} {st.median(tv[k]) if tv[k] else 0:7.0f} |"
              f" {pf('pre_render')} {pf('views')} {pf('view_setup')} {pf('view_submit')} {pf('scene_end')} {pf('present')} sec={sidx[0][1]}")
