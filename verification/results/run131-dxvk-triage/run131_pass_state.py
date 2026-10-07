"""Run 131 A (DXVK) triage: per-frame pass state aggregates from a proxy session log.
Usage: python3 run131_pass_state.py <session.log>   (streams; prints counts only)"""
import sys, collections, re
KV = re.compile(r'(\w+)=(\S+)')
FIELDS = {
 'hdr_frame': ['redirected', 'target_create', 'latch_bind', 'tonemapped', 'end', 'unwind_reason', 'blocked', 'suspended', 'meter', 'readback', 'tonemap_draw', 'caps'],
 'motion_output_frame': ['latched', 'routed', 'matched', 'taa_attempted', 'taa_resolved', 'taa_skip', 'taa_result', 'scene_open', 'hook_signals', 'hook_outside_scene', 'hook_state', 'draws_after_hook', 'camera_valid', 'camera_reads', 'selector_state', 'jittered', 'fill_result', 'scene_end_source', 'bloom_copy_seen'],
 'taa_invalidate': ['site'],
 'camera_state': ['valid', 'reads', 'read_failure', 'failure'],
 'fade_route_frame': None, 'thin_vote_frame': None, 'shadow_lease_retirement': None,
}
cnt = {k: collections.defaultdict(collections.Counter) for k in FIELDS}
draws = []; dts = []; nframes = collections.Counter()
with open(sys.argv[1], 'rb') as f:
    for raw in f:
        sp = raw.find(b' ')
        if sp < 0: continue
        k = raw[:sp].decode('latin1')
        if k == 'frame_end':
            d = dict(KV.findall(raw.decode('latin1')))
            draws.append(int(d.get('draws', 0))); dts.append(int(d.get('dt_ms', 0)))
            continue
        if k not in FIELDS or FIELDS[k] is None: continue
        d = dict(KV.findall(raw.decode('latin1')))
        if 'device' not in d: continue
        nframes[k] += 1
        for fld in FIELDS[k]:
            v = d.get(fld, '<absent>')
            if fld in ('routed', 'matched', 'camera_reads', 'reads', 'draws_after_hook', 'hook_signals', 'taa_attempted', 'taa_resolved'):
                v = '0' if v == '0' else ('>0' if v != '<absent>' else v)
            cnt[k][fld][v] += 1
for k in cnt:
    if not cnt[k]: continue
    print(f'== {k} rows={nframes[k]}')
    for fld, c in cnt[k].items():
        print(f'  {fld}: ' + ', '.join(f'{v}:{n}' for v, n in c.most_common(6)))
busy = [i for i, d in enumerate(draws) if d >= 500]
print(f'== frame_end rows={len(draws)} max_draws={max(draws)} frames_draws>=500={len(busy)}')
if busy:
    bd = sorted(dts[i] for i in busy)
    print(f'  dt_ms over draws>=500: median={bd[len(bd)//2]} p95={bd[int(len(bd)*.95)]} n={len(bd)}')
ad = sorted(dts); print(f'  dt_ms all: median={ad[len(ad)//2]} p95={ad[int(len(ad)*.95)]}')
