"""Compare the Run78 motion summary's dither cases with the dither ledger's record (non-volatile leaves only).
usage: motion_dither_compare.py <ledger cases json> <run motion-output-summary.json>"""
import json, sys
ref = json.load(open(sys.argv[1])); n = json.load(open(sys.argv[2]))
by = lambda c: c if isinstance(c, dict) else {x['name']: x for x in c}
rc, nc = by(ref['cases']), by(n['cases'])
VOL = {'directory', 'dll_sha256', 'exe_sha256', 'trace_sha256', 'elapsed', 'seconds', 'exit'}
def leaves(d, p=''):
    if isinstance(d, dict):
        for k, v in d.items():
            if k in VOL or 'time' in k or k.endswith('_ms') or 'sha' in k: continue
            yield from leaves(v, p + '/' + k)
    elif isinstance(d, list):
        for i, v in enumerate(d): yield from leaves(v, f'{p}[{i}]')
    else: yield p, d
tot = diff = 0
for k in rc:
    a = dict(leaves(rc[k])); b = dict(leaves(nc.get(k, {})))
    common = set(a) & set(b); d = [x for x in common if a[x] != b[x]]
    tot += len(common); diff += len(d)
    print(k, 'in_run' if k in nc else 'MISSING', 'common', len(common), 'differ', len(d), d[:3])
print('cases', len(rc), 'common_leaves', tot, 'differing', diff)
