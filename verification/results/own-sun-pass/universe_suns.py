import sys, re
import xml.etree.ElementTree as ET
from collections import Counter
mapfile, tsuns = sys.argv[1], sys.argv[2]
# TSuns: header "ver;count;" then rows; row field 8 = lens group, field 9 = fixed-point factor, last = name
rows = []
for line in open(tsuns, encoding='latin-1'):
    line = line.split('//')[0].strip()
    if not line or line.count(';') < 10: continue
    f = [x.strip() for x in line.split(';')]
    rows.append(dict(model=f[0], rot=f[4], group=int(f[8]), scale=int(f[9]), name=f[18]))
print('tsuns rows', len(rows))
root = ET.fromstring(open(mapfile, 'rb').read())
sectors = [o for o in root.iter('o') if o.get('t') == '1']
per_sector = Counter(); subtype = Counter(); groups = Counter(); scale = Counter(); multi = 0; nosun = 0
sunsets = Counter()
for sec in sectors:
    suns = [c for c in sec.findall('o') if c.get('t') == '3']
    per_sector[len(suns)] += 1
    if not suns: nosun += 1
    key = []
    for s in suns:
        i = int(s.get('s'))
        subtype[i] += 1
        if i < len(rows):
            groups[rows[i]['group']] += 1; scale[rows[i]['scale']] += 1
            key.append((rows[i]['group'], rows[i]['scale']))
        else:
            key.append(('OOB', i))
    sunsets[tuple(sorted(key))] += 1
print('sectors', len(sectors), 'suns per sector', dict(sorted(per_sector.items())))
print('subtype index histogram', dict(sorted(subtype.items())))
print('lens groups referenced', dict(sorted(groups.items())), 'distinct', len(groups))
print('scale field histogram', dict(sorted(scale.items())))
print('distinct (group,scale) sun sets per sector', len(sunsets))
# attributes seen on sun objects
attrs = Counter()
for sec in sectors:
    for s in sec.findall('o'):
        if s.get('t') == '3':
            for k in s.attrib: attrs[k] += 1
print('sun object attributes', dict(attrs))
ex = [s.attrib for sec in sectors[:3] for s in sec.findall('o') if s.get('t') == '3']
print('examples', ex[:4])
