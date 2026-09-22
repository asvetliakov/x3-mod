#!/usr/bin/env python3
"""Alpha-textured faces in the coarsest record of the pilot bodies (numbers only).

A material counts as alpha when its effect parameter t_AlphaTexture names a real
texture (not NULL, not a NONE_* placeholder such as NONE_WHITE.dds), the rule of
lod_overlay.alpha_materials. Also lists the NONE_* placeholder faces, which that
rule treats as opaque, and the alpha-test/blend flags of each alpha material.

  python3 verification/results/lod-overlay-pilot/alpha_faces.py [body ...]
"""
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402

PILOT = ('ships/argon/argon_TL', 'ships/argon/argon_M2', 'ships/argon/argon_M1',
         'stations/others/military_outpost_middleb')


def param(m, name):
    return next((v for k, _, v in m.get('params', ()) if k.lower() == name), None)


for body in sys.argv[1:] or PILOT:
    data, origin = bob1.load(body)
    tree = bob1.parse(data)
    mats = bob1.materials(tree)
    alpha = lod_overlay.alpha_materials(mats)
    last = bob1.lods(tree)[-1]
    faces = Counter()
    for p in last['parts']:
        for g in p['groups']:
            faces[g['material']] += len(g['faces'])
    placeholder = sum(n for m, n in faces.items() if m not in alpha and 0 <= m < len(mats)
                      and param(mats[m], b't_alphatexture') not in (None, b'', b'NULL'))
    rows = [(m, n, param(mats[m], b't_alphatexture').decode(errors='replace').rsplit('\\', 1)[-1],
             (param(mats[m], b'g_alphatestenable') or [None])[0], (param(mats[m], b'g_alphablendenable') or [None])[0])
            for m, n in faces.most_common() if m in alpha]
    print(f'{origin}: coarsest LOD{len(bob1.lods(tree)) - 1} faces {sum(faces.values())} groups'
          f' {sum(len(p["groups"]) for p in last["parts"])} alpha faces {sum(r[1] for r in rows)}'
          f' in {len(rows)} materials; NONE_* placeholder faces (opaque) {placeholder}')
    for m, n, tex, test, blend in rows:
        print(f'  mat{m} faces {n} alpha {tex} alphatest {test} alphablend {blend}')
