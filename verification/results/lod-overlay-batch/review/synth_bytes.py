import sys, json, tempfile, io, contextlib, unittest.mock
from pathlib import Path
R = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(R / 'verification/analysis'), str(R / 'tools/analysis'), str(R / 'verification/probe')]
import bob1, lod_overlay, lod_atlas
from sector_fog_census import unpack
from test_lod_overlay_batch import make_game, BATCH, cat_members
from test_bob1 import atlas_tree_lod0
def records_ok(lod):
    bad = 0
    for p in lod['parts']:
        if not p['flags'] & bob1.PART_PRECOMPUTED: continue
        for g in p['groups']:
            order = list(dict.fromkeys(i for f in g['faces'] for i in f[:3]))
            if [r[0] for r in g.get('extra', [])] != order: bad += 1
    return bad
with tempfile.TemporaryDirectory() as d:
    game = make_game(d); out = Path(d) / 'o'
    with unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[]), contextlib.redirect_stdout(io.StringIO()):
        lod_overlay.main(BATCH + ['--game', str(game), '--out', str(out)])
    marker = json.loads((out / 'addon/02.x3m-lod.json').read_text())
    print('marker top-level collapse', marker.get('collapse'), 'atlas_options present', 'atlas_options' in marker,
          'bodies collapse', sorted({b['collapse'] for b in marker['bodies']}))
    mem = cat_members(out / 'addon/02.cat')
    src = {'ships/x/mixed': None, 'ships/x/uv': None}
    import test_lod_overlay_batch as T
    srcs = {'ships/x/mixed': T.mixed_tree(), 'ships/x/uv': T.uv2_tree(), 'ships/x/good': atlas_tree_lod0()}
    for name, stree in srcs.items():
        tree = bob1.parse(unpack(mem[f'objects/{name}.pbb']))
        L, mats = bob1.lods(tree), bob1.materials(tree)
        s0 = bob1.lods(stree)[0]; smats = bob1.materials(stree)
        c = L[1]
        print(name, 'ladder', [l['value'] for l in L], 'mats', len(smats), '->', len(mats),
              'new mats', [(m.get('index'), m['effect'], [v for n, t, v in m['params'] if t == 8][:2]) for m in mats[len(smats):]])
        print('  C groups', [(pi, g['material'], len(g['faces'])) for pi, p in enumerate(c['parts']) for g in p['groups']],
              'record-order mismatches', records_ok(c), 'R0 identical', L[0] == s0)
        flags = {p[0] for p in c['points']}
        print('  C point flags', sorted(flags), 'source flags', sorted({p[0] for p in s0['points']}))
        if name == 'ships/x/uv':
            # every C point's uv2 equals some source point's uv2 via position match
            spos = {}
            for p in s0['points']: spos.setdefault(p[1:4], set()).add(p[6:8])
            print('  uv2 pass-through ok', all(p[6:8] in spos.get(p[1:4], ()) for p in c['points']))
    names = sorted(k for k in mem if k.startswith('dds/'))
    print('atlas members', len(names), 'e.g.', names[:2])
    # every atlas texture name referenced by a written material resolves to a member of this catalogue
    refs = set()
    for k, v in mem.items():
        if k.startswith('objects/'):
            t = bob1.parse(unpack(v)) if v[:4] != b'BOB1' else bob1.parse(v)
            for m in bob1.materials(t):
                for n, ty, val in m.get('params', ()):
                    if ty == 8 and val.lower().startswith(b'x3m_lod\\'):
                        refs.add('dds/' + val.decode()[8:].rsplit('.', 1)[0].lower() + '.pck')
    print('material refs', len(refs), 'unresolved', sorted(refs - {n.lower() for n in names}), 'unreferenced slots', sorted({n.rsplit('_',1)[1] for n in names if n.lower() not in refs}))
