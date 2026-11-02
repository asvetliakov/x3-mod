import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
from inspect_x3 import read_catalogue
out = Path(sys.argv[1]) / 'addon2'
game = Path('/mnt/c/X3FL/addon2')
LIM = 2**31 - 1
slot_of = {}
for s in (6, 7):
    cat = out / f'{s:02d}.cat'
    raw = cat.read_bytes()
    lines = bytes(v ^ ((0xdb + i) & 255) for i, v in enumerate(raw)).decode()
    print(s, 'header', lines.splitlines()[0], 'trailing-newline', lines.endswith('\n'), 'crlf', '\r' in lines)
    es = read_catalogue(cat)
    mx = max(e['offset'] + e['size'] for e in es)
    print(s, 'entries', len(es), 'max end', mx, 'below limit', mx <= LIM, 'max start', max(e['offset'] for e in es))
    for e in es:
        k = e['path'].lower()
        if k in slot_of: print('DUP across slots', k)
        slot_of[k] = s
    m = json.loads((out / f'{s:02d}.x3m-lod.json').read_text())
    print(s, 'marker slot', m['slot'], 'overlay_slots', m.get('overlay_slots'), 'edition', m.get('edition'), 'bodies', len(m['bodies']))
    split = 0
    for b in m['bodies']:
        mem = [x['path'].lower() for x in b.get('members', [])]
        ss = {slot_of.get(x) for x in mem}
        if ss != {s}: split += 1; print('  body split or misplaced', b.get('name'), ss) if split < 4 else None
    print(s, 'bodies whose members are not all in this slot', split)
# stock format comparison
st = (game / '01.cat').read_bytes()
sl = bytes(v ^ ((0xdb + i) & 255) for i, v in enumerate(st)).decode('latin-1')
print('stock header', sl.splitlines()[0], 'trailing-newline', sl.endswith('\n'), 'crlf', '\r' in sl, 'sep sample', repr(sl.splitlines()[1][-12:]))
