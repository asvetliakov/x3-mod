"""Across all refused no_dust_bodies families: what files their nebula dirs hold, whether any
'dust' member or texture mentions the family anywhere, and loose TBackgrounds vs cat copies."""
import json, re, collections, hashlib
from pathlib import Path
import sector_fog_census as sfc
GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
a = sfc.Assets(GAME)
res = json.load(open(GAME / 'x3m/fog-families.json'))
nodust = [f['name'] for f in res['families'] if f['reason'] == 'no_dust_bodies']
kinds = collections.Counter(); dusthits = collections.Counter(); shapes = collections.Counter()
for fam in nodust:
    pat = re.compile(re.escape(fam) + r'(?![0-9])')
    keys = [k for k in a.entries if pat.search(k)]
    shape = tuple(sorted(re.sub(re.escape(fam), '<F>', k) for k in keys))
    shapes[shape] += 1
    for k in keys:
        if 'dust' in k:
            dusthits[fam] += 1
print('no_dust_bodies families', len(nodust), 'distinct member-name shapes', len(shapes))
for s, n in shapes.most_common(5):
    print(n, s)
print('families with any member containing "dust":', len(dusthits), dict(list(dusthits.items())[:5]))
# all dust-part bodies anywhere, grouped by family dir
dp = collections.Counter(m.group(1) for k in a.entries if (m := re.match(r'objects/environments/nebulae/([^/]+)/.*dust_part', k)))
print('dirs with dust_part bodies', len(dp), 'of which litcube', sum(k.startswith('litcube') for k in dp))
# loose vs catalogue copies of TBackgrounds and universe
for p in ('types/tbackgrounds.txt', 'maps/x3_universe.xml'):
    for e in a.candidates(p):
        d = a.read_entry(e)
        print(p, e['source'], len(d), hashlib.sha256(d).hexdigest()[:16])
