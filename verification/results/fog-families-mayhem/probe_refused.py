"""Read-only probe of refused Mayhem fog families (host Python; no Wine).
Usage: PYTHONPATH=tools/analysis python3 probe_refused.py [family ...]
Prints: TBackgrounds rows (effective source), name-matching members in all layers,
sector references in the effective x3_universe, dust-part resolution and nebulafog textures."""
import json, re, sys, collections, xml.etree.ElementTree as ET
from pathlib import Path
import sector_fog_census as sfc
import fog_families as ff

GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
assets = sfc.Assets(GAME)
bg, bgsrc = assets.logical('types/tbackgrounds', ('.pck', '.txt'))
print('TBackgrounds effective:', bgsrc['source'], bgsrc['member'])
print('TBackgrounds all layers:', [e['source'] for e in assets.candidates('types/tbackgrounds.txt')])
lines = [l.strip() for l in bg.decode('utf-8-sig').splitlines() if l.strip() and not l.lstrip().startswith('//')]
raw = [l.rstrip(';').split(';') for l in lines[1:]]
rows = sfc.backgrounds(bg)
mp, mpsrc = assets.logical('maps/x3_universe', ('.pck', '.xml'))
print('universe effective:', mpsrc['source'], mpsrc['member'],
      'all layers:', [e['source'] for e in assets.candidates('maps/x3_universe.xml')])
per_index = collections.Counter()
nsect = 0
for o in ET.fromstring(mp).iter('o'):
    if o.get('t') != '1':
        continue
    nsect += 1
    for c in o.findall('o'):
        if c.get('t') == '2':
            per_index[int(c.get('s'))] += 1
print('sectors', nsect, 'background refs by attribute s (row index)')
res = json.load(open(GAME / 'x3m/fog-families.json'))
status = {f['name']: (f['status'], f['reason']) for f in res['families']}
per_family = collections.Counter()
for i, n in per_index.items():
    per_family[rows[i]['family']] += n
ref = [n for n, s in status.items() if s[0] == 'refused']
ok = [n for n, s in status.items() if s[0] in ('ok', 'covered_by_build')]
print('refused families', len(ref), 'with >=1 sector', sum(per_family[n] > 0 for n in ref),
      'sectors on refused', sum(per_family[n] for n in ref))
print('ok families', len(ok), 'with >=1 sector', sum(per_family[n] > 0 for n in ok), 'sectors on ok', sum(per_family[n] for n in ok))
# directory census of objects/environments/nebulae/<fam>/ over all keys
dirs = collections.defaultdict(list)
for key, entries in assets.entries.items():
    m = re.match(r'(?:addon/)?objects/environments/nebulae/([^/]+)/', key)
    if m:
        dirs[m.group(1)].extend(e['source'] for e in entries)
print('nebula dirs total', len(dirs), 'litcube dirs', sum(k.startswith('litcube') for k in dirs))
print('refused with a nebula dir', sum(n.lower() in dirs for n in ref), '; ok with a nebula dir', sum(n.lower() in dirs for n in ok))

for fam in sys.argv[1:]:
    print('\n==', fam, status.get(fam))
    idx = [r['index'] for r in rows if r['family'] == fam]
    for i in idx:
        v = raw[i]
        print(f' row {i}: name={v[7]} stars/bg/outside={v[8]}/{v[9]}/{v[10]} rates={v[11:19]} dust={v[19]} '
              f'colA={v[20:23]} c23={v[23]} near={v[24]} far={v[25]} stardust%={v[26]} colB={v[27:30]} c0-6={v[0:7]} c30-36={v[30:37]} id={v[37]} sectors={per_index[i]}')
    pat = re.compile(re.escape(fam.lower()) + r'(?![0-9])')
    hits = sorted((k, e['source']) for k, es in assets.entries.items() for e in es if pat.search(k))
    bydir = collections.Counter(k.rsplit('/', 1)[0] for k, _ in hits)
    print(' members with name:', len(hits), dict(bydir))
    for k, s in hits[:6]:
        print('   ', s, k)
    for slot in range(1, 9):
        data, src = assets.logical(f'objects/environments/nebulae/{fam}/nebula_{fam}_dust_part{slot:02d}', ff.BODY_EXTENSIONS)
        if data is None:
            continue
        mats = ff.body_materials(data, src['member'])
        for eff, tex in mats:
            stem = ff.texture_stem(tex) if tex and tex != '0' else None
            t = assets.logical(stem, ff.TEXTURE_EXTENSIONS)[1] if stem else None
            print(f'  part{slot:02d} {src["source"]} {eff} tex={tex} stem={stem} resolves={t["source"] if t else None}')
