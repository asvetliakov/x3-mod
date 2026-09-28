"""Parsed-row comparison of the 07.cat and loose TBackgrounds / x3_universe background refs."""
import collections, xml.etree.ElementTree as ET
from pathlib import Path
import sector_fog_census as sfc
GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
a = sfc.Assets(GAME)
tb = {e['source']: e for e in a.candidates('types/tbackgrounds.txt')}
um = {e['source']: e for e in a.candidates('maps/x3_universe.xml')}
L = 'loose:addon/types/TBackgrounds.txt'
r7, rl = sfc.backgrounds(a.read_entry(tb['addon/07.cat'])), sfc.backgrounds(a.read_entry(tb[L]))
print('rows 07/loose', len(r7), len(rl), 'identical parsed rows', r7 == rl)
def refs(e):
    c = collections.Counter()
    for o in ET.fromstring(a.read_entry(e)).iter('o'):
        if o.get('t') == '1':
            for ch in o.findall('o'):
                if ch.get('t') == '2': c[(int(o.get('x')), int(o.get('y')))] = int(ch.get('s'))
    return c
u7, ul = refs(um['addon/07.cat']), refs(um['loose:addon/maps/x3_universe.xml'])
print('sectors 07/loose', len(u7), len(ul), 'identical sector->s map', u7 == ul)
for name, rows, u in (('07.cat', r7, u7), ('loose', rl, ul)):
    per = collections.Counter(rows[s]['family'] for s in u.values() if s < len(rows))
    oob = sum(s >= len(rows) for s in u.values())
    fams = {r['family'] for r in rows}
    print(name, 'out-of-range s', oob, 'families', len(fams),
          {f: ([r['index'] for r in rows if r['family'] == f], per[f]) for f in ('litcube9', 'litcube20', 'litcube59', 'litcube120', 'litcube203', 'litcube1')})
print('families only in 07', sorted({r['family'] for r in r7} - {r['family'] for r in rl}),
      'only in loose', sorted({r['family'] for r in rl} - {r['family'] for r in r7}))
