"""Join frame_phases_slow (per-frame view_setup_us, pre_render_us) with hdr_frame readback_transfer_lock_us
of the same and neighbouring frame numbers; prints offset fit and the joined rows. Usage: slow_join.py <log>"""
import sys, re, statistics as st
KV = re.compile(r'(\w+)=(-?[0-9.]+)(?=\s|$)')
slow = []; hdr = {}; fe = {}
for raw in open(sys.argv[1], 'rb'):
    n = raw.split(b' ', 1)[0]
    if n == b'frame_phases_slow': slow.append({k: float(v) for k, v in KV.findall(raw.decode('latin1'))})
    elif n == b'hdr_frame':
        d = dict(KV.findall(raw.decode('latin1'))); hdr[int(d['frame'])] = float(d.get('readback_transfer_lock_us', 'nan'))
    elif n == b'frame_end':
        d = dict(KV.findall(raw.decode('latin1'))); fe[int(d['frame'])] = float(d['dt_ms'])
# which offset of hdr frame numbering best matches: correlation of view_setup with lock
def corr(a, b):
    ma, mb = st.mean(a), st.mean(b); sa = sum((x-ma)**2 for x in a)**.5; sb = sum((y-mb)**2 for y in b)**.5
    return sum((x-ma)*(y-mb) for x, y in zip(a, b)) / (sa*sb) if sa and sb else float('nan')
for off in (-2, -1, 0, 1, 2):
    pr = [(s['view_setup_us'], hdr[int(s['frame'])+off]) for s in slow if int(s['frame'])+off in hdr]
    if len(pr) > 3: print(f'offset {off:+d} n={len(pr)} corr(view_setup,lock)={corr(*zip(*pr)):.3f}')
# frame_end dt alignment check
for off in (-1, 0, 1):
    pr = [(s['dt_us']/1000, fe[int(s['frame'])+off]) for s in slow if int(s['frame'])+off in fe]
    print(f'frame_end offset {off:+d} n={len(pr)} median|dt_slow-dt_end|_ms={st.median(abs(a-b) for a,b in pr):.2f}')
print('frame view_setup_us lock_us(same) pre_render_us views_us view_submit_us dt_us')
for s in slow:
    f = int(s['frame']); print(f, int(s['view_setup_us']), hdr.get(f), int(s['pre_render_us']), int(s['views_us']), int(s['view_submit_us']), int(s['dt_us']), int(s.get('views', -1)))
