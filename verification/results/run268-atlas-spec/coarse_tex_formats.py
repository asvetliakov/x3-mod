"""Run 268: first texture/texture_desc pair per identity for the given identities (stage, levels, WxH, format fourcc).
usage: coarse_tex_formats.py LOG ID..."""
import sys, re
log, ids = sys.argv[1], set(sys.argv[2:]); kv = re.compile(r'(\w+)=(\S+)'); seen = {}; pend = None
def fcc(v):
    v = int(v); b = v.to_bytes(4, 'little')
    return b.decode() if all(65 <= c <= 90 or 48 <= c <= 57 for c in b) else str(v)
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('texture '):
            d = dict(kv.findall(line)); pend = d if d.get('identity') in ids and d['identity'] not in seen else None
        elif pend and line.startswith('texture_desc '):
            d = dict(kv.findall(line))
            if d['stage'] == pend['stage']: seen[pend['identity']] = (pend['stage'], pend['levels'], d['w'], d['h'], fcc(d['format']))
            pend = None
for i in sorted(ids, key=int): print(i, *seen.get(i, ('missing',)))
