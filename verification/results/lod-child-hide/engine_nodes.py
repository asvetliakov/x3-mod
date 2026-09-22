"""Engine-effect nodes in the cull census of one frame: body, verdict, s, d, flags. Usage: LOG FRAME"""
import sys,re,collections
kv=re.compile(r'(\w+)=(\S+)')
tag=f'cull_census device=1 frame={sys.argv[2]} '
rows=[]
with open(sys.argv[1], errors='replace') as fh:
    for line in fh:
        if not line.startswith(tag): continue
        d=dict(kv.findall(line))
        if 'engines' in d.get('body',''): rows.append(d)
agg=collections.Counter((r['body'].split('\\')[-1], r['verdict'], r['flags_in']) for r in rows)
for k,v in sorted(agg.items()): print(v, *k)
print('kept rows:')
for r in rows:
    if r['verdict']!='culled_min': print(' ', r['body'].split('\\')[-1], 's=',r['s'],'d=',r['d'],'radius=',r['radius'],'lod=',r['lod'],r['flags_in'],r['flags_out'],r['verdict'])
