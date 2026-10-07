"""Run 131 A (DXVK): per-frame aggregates of engine plumes, shadows, fade route, thin vote, hull emission, fog rows.
Usage: python3 run131_other_passes.py <session.log>"""
import sys, re, collections
KV = re.compile(r'(\w+)=(\S+)')
WANT = {'engine_stage': ['armed', 'reason', 'ran'], 'engine_frame': ['records', 'suppressed', 'candidates'],
        'shadow_lease_retirement': ['records'], 'fade_route_frame': ['fade_routed', 'fade_refused'],
        'thin_vote_frame': ['lane', 'voted'], 'hull_emission_frame': ['admitted', 'refused_state'],
        'volumetric_fog_prefill': ['event', 'action'], 'volumetric_fog_cache': ['event', 'reason']}
SUM = {'engine_frame': ['records', 'suppressed'], 'hull_emission_frame': ['admitted'], 'fade_route_frame': ['fade_routed'],
       'thin_vote_frame': ['voted'], 'shadow_lease_retirement': ['records']}
c = {k: collections.defaultdict(collections.Counter) for k in WANT}; s = collections.Counter(); n = collections.Counter()
with open(sys.argv[1], 'rb') as f:
    for raw in f:
        k = raw[:raw.find(b' ')].decode('latin1')
        if k not in WANT: continue
        d = dict(KV.findall(raw.decode('latin1'))); n[k] += 1
        for fld in WANT[k]:
            v = d.get(fld, '<absent>')
            if k in SUM and fld in SUM[k]:
                s[(k, fld)] += int(v) if v.isdigit() else 0; v = '0' if v == '0' else '>0'
            c[k][fld][v] += 1
for k in WANT:
    print(f'== {k} rows={n[k]}')
    for fld, cc in c[k].items(): print(f'  {fld}: ' + ', '.join(f'{v}:{m}' for v, m in cc.most_common(5)))
for (k, fld), v in s.items(): print(f'sum {k}.{fld}={v}')
