"""Write race18_parts.txt: part bodies referenced by every scene that a TDocks/TFactories row with race field [13] == 18
(row+0x5c, the 0x00441639 test) names in field [11]. Read-only archive access. usage: race18_parts.py GAME_ROOT OUT"""
import sys, re
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools' / 'analysis'))
import sector_fog_census as sfc
a = sfc.Assets(Path(sys.argv[1])); sc = set()
for t in ('TDocks', 'TFactories'):
    d, _ = a.logical(f'types/{t}', ('.pck', '.txt'))
    for l in [l for l in d.decode('latin-1').splitlines() if l and not l.startswith('/')][1:]:
        f = l.split(';')
        if int(f[13]) == 18: sc.add(f[11].lower().replace('\\', '/'))
parts = set()
for s in sc:
    data = None
    for ext in ('.pbd', '.bod', '.pbb', '.bob'):
        try: data, _ = a.get('objects/' + s + ext); break
        except FileNotFoundError: pass
    if data is None: continue
    toks = {t.lower().replace('\\', '/') for t in re.findall(rb'[A-Za-z0-9_]+(?:\\[A-Za-z0-9_]+)+', data) for t in [t.decode()]}
    toks = {t[1:] if t.startswith('b') and t[1:] in toks else t for t in toks}
    parts |= {t for t in toks if t.split('/')[0] in ('stations', 'patch20', 'environments')}
Path(sys.argv[2]).write_text('\n'.join(sorted(parts)) + '\n'); print(len(sc), 'scenes', len(parts), 'parts')
