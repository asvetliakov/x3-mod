import sys, gzip, re
sys.path.insert(0, '/home/asvetl/x3-mod/tools/analysis')
from inspect_x3 import read_catalogue
from catalogue_mounts import mounted_catalogues
def unpack(d):
    if d.startswith(b'\x1f\x8b'): return gzip.decompress(d)
    if len(d) > 3:
        k = d[0] ^ 0xc8
        if bytes(v ^ k for v in d[1:3]) == b'\x1f\x8b': return gzip.decompress(bytes(v ^ k for v in d[1:]))
    return d
game, edition = sys.argv[1], sys.argv[2]
idx = {}
for cat in mounted_catalogues(game, edition):
    for e in read_catalogue(cat): idx[e['path'].replace('\\','/').lower()] = dict(e, cat=cat)
for bid in sys.argv[3:]:
    hits = [k for k in idx if re.fullmatch(r'(addon2?/)?objects/v/%05d\.(pbd|bod|pbb|bob)' % int(bid), k)]
    if not hits: print(bid, 'MISSING'); continue
    k = sorted(hits)[-1]; e = idx[k]
    with e['cat'].with_suffix('.dat').open('rb') as s:
        s.seek(e['offset']); raw = s.read(e['size'])
    d = unpack(bytes(v ^ 0x33 for v in raw))
    if d[:4] == b'BOB1': print(bid, k, e['cat'].name, 'BOB1 binary', len(d)); continue
    t = d.decode('latin-1', 'replace')
    name = re.search(r'^//\s*(.+)$', t, re.M)
    mats = re.findall(r'^MATERIAL\d*:.*$', t, re.M)
    size = re.search(r'^(\d+);\s*//.*automatic', t, re.M | re.I)
    print(bid, k, e['cat'].name, 'name=%r' % (name.group(1).strip() if name else ''), 'materials=', [m[:90] for m in mats[:3]], 'lines=%d' % t.count('\n'))
