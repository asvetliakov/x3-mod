import sys, gzip, hashlib
from pathlib import Path
sys.path.insert(0, '/home/asvetl/x3-mod/tools/analysis')
from inspect_x3 import read_catalogue
from catalogue_mounts import mounted_catalogues

def unpack(data):
    if data.startswith(b'\x1f\x8b'):
        return gzip.decompress(data)
    if len(data) > 3:
        key = data[0] ^ 0xc8
        if bytes(v ^ key for v in data[1:3]) == b'\x1f\x8b':
            return gzip.decompress(bytes(v ^ key for v in data[1:]))
    return data

def canon(p):
    return p.replace('\\', '/').lower()

def build(game, edition):
    index = {}
    for cat in mounted_catalogues(game, edition):
        for e in read_catalogue(cat):
            index[canon(e['path'])] = dict(e, cat=cat)
    return index

def get(index, *names):
    for n in names:
        e = index.get(canon(n))
        if e:
            with e['cat'].with_suffix('.dat').open('rb') as s:
                s.seek(e['offset']); raw = s.read(e['size'])
            data = unpack(bytes(v ^ 0x33 for v in raw))
            return data, e['cat'].name, e['path']
    return None, None, None

game, edition, out = sys.argv[1], sys.argv[2], Path(sys.argv[3])
out.mkdir(parents=True, exist_ok=True)
idx = build(game, edition)
exp = 'addon2' if edition == 'fl' else 'addon'
for stem in ('types/TSuns', 'types/Lensflares', 'maps/x3_universe', 'types/TPlanets'):
    for prefix in ('', exp + '/'):
        for ext in ('.pck', '.txt', '.xml'):
            data, cat, path = get(idx, prefix + stem + ext)
            if data:
                name = (prefix + stem).replace('/', '_') + ext
                (out / name).write_bytes(data)
                print(f'{path} <- {cat} {len(data)} bytes sha256={hashlib.sha256(data).hexdigest()[:16]}')
