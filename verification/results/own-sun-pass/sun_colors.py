import sys
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
mapfile, tsuns = sys.argv[1], sys.argv[2]
rows = []
for line in open(tsuns, encoding='latin-1'):
    line = line.split('//')[0].strip()
    if not line or line.count(';') < 10: continue
    f = [x.strip() for x in line.split(';')]
    rows.append(dict(group=int(f[8]), scale=int(f[9]), name=f[18]))
root = ET.fromstring(open(mapfile, 'rb').read())
colors = Counter(); bygroup = defaultdict(Counter); flags = Counter()
for sec in root.iter('o'):
    if sec.get('t') != '1': continue
    for s in sec.findall('o'):
        if s.get('t') != '3': continue
        c = int(s.get('color')); g = rows[int(s.get('s'))]['group']
        colors[c] += 1; bygroup[g][c] += 1; flags[s.get('f')] += 1
print('distinct colors', len(colors), 'flags', dict(flags))
for g in sorted(bygroup):
    cs = bygroup[g]
    print('group', g, 'suns', sum(cs.values()), 'distinct colors', len(cs), 'top:',
          ['#%06x x%d' % (c, n) for c, n in cs.most_common(4)])
