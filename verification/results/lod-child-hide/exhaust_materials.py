"""Per pilot ship: faces of exhaust/engine-named materials in the coarsest original record and C's materials.
Usage: python3 exhaust_materials.py (from tools/analysis)."""
import sys, collections
sys.path.insert(0, '.')
import bob1
for body in ('ships/argon/argon_TL', 'ships/argon/argon_M2', 'ships/argon/argon_M1'):
    data, prov = bob1.load(body)
    tree = bob1.parse(data)
    mats = {m['index']: m for m in bob1.materials(tree)}
    L = bob1.lods(tree)
    def tex(mi):
        m = mats[mi]
        return {(a.decode() if isinstance(a, bytes) else a): (b.decode('latin1') if isinstance(b, bytes) else b)
                for a, t, b in m.get('params', []) if t == 8}
    def faces(rec):
        c = collections.Counter()
        for p in rec['parts']:
            for g in p['groups']: c[g['material']] += len(g['faces'])
        return c
    n = len(L)
    orig_last = L[n - 3]            # coarsest original record (pad placement appends C and pad)
    c_rec = L[n - 2]
    fo = faces(orig_last)
    ex = {mi: k for mi, k in fo.items() if any(w in tex(mi).get('t_DiffuseTexture', '').lower() for w in ('exhaust', 'engine', 'thruster'))}
    lm = {mi: k for mi, k in ex.items() if tex(mi).get('t_LightMapTexture', 'NULL').upper() not in ('', 'NULL')}
    fc = faces(c_rec)
    print(prov, 'records', n, 'orig coarsest index', n - 3, 'faces', sum(fo.values()),
          'exhaust-material faces', sum(ex.values()), 'of which light-mapped', sum(lm.values()), 'materials', sorted(ex),
          '| C index', n - 2, 'materials', sorted(fc), 'C exhaust materials', sorted(set(fc) & set(ex)))
