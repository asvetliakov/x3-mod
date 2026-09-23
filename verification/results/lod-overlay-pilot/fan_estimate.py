#!/usr/bin/env python3
"""Estimate of the vertices D3DXCleanMesh adds to each group mesh at load (read only).

The engine builds one ID3DXMesh per group (0x004bb470) and runs 0x004bc680 on it:
GenerateAdjacency with epsilon ~1e-6 (positions welded), then D3DXCleanMesh with flags 3
(D3DXCLEAN_BACKFACING | D3DXCLEAN_BOWTIES), before the vertex count matters for the 16-bit
clone (loading-observations.md, mesh-buffer-rewrite.md). Estimate per group, on the group's
own vertices (one per distinct point index):
  bowtie   for every vertex, its faces are joined when they share an edge through it whose
           other end has the same position (welded adjacency); every fan beyond the first
           is one more vertex;
  backface for every pair of faces with the same three positions in opposite winding that
           share a vertex index, one more vertex per shared index.
This is an upper-bound style estimate of D3DX's behaviour (the exact rules are D3DX's, not
traced); the pilot's split limit (lod_atlas.MAX_GROUP_POINTS) keeps its headroom above it.

  python3 verification/results/lod-overlay-pilot/fan_estimate.py <overlay root> [--source]
    --source: also estimate the shipped source records of the same bodies
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
import bob1  # noqa: E402
import lod_overlay  # noqa: E402
from inspect_x3 import read_catalogue  # noqa: E402
from sector_fog_census import unpack  # noqa: E402


def find(parent, x):
    while parent[x] != x:
        parent[x] = parent[parent[x]]
        x = parent[x]
    return x


def estimate(points, faces):
    pos = lambda i: tuple(points[i][1:4])
    around = {}
    for fi, f in enumerate(faces):
        for k in range(3):
            around.setdefault(f[k], []).append((fi, k))
    bowtie = 0
    for v, inc in around.items():
        if len(inc) < 2:
            continue
        parent = {fi: fi for fi, _ in inc}
        edges = {}
        for fi, k in inc:
            f = faces[fi]
            for other in (f[(k + 1) % 3], f[(k + 2) % 3]):
                e = pos(other)
                if e in edges:
                    a, b = find(parent, edges[e]), find(parent, fi)
                    parent[a] = b
                else:
                    edges[e] = fi
        bowtie += len({find(parent, fi) for fi, _ in inc}) - 1
    seen, backface = {}, 0
    for f in faces:
        p = [pos(i) for i in f[:3]]
        key = frozenset(p)
        if len(key) < 3:
            continue
        rot = min((p[k], p[(k + 1) % 3], p[(k + 2) % 3]) for k in range(3))
        rev = min((p[k], p[(k + 2) % 3], p[(k + 1) % 3]) for k in range(3))
        for g in seen.get((key, rev), ()):
            backface += len(set(f[:3]) & set(g[:3]))
        seen.setdefault((key, rot), []).append(f)
    return bowtie, backface


def report(label, lod):
    for pi, part in enumerate(lod['parts']):
        for gi, g in enumerate(part['groups']):
            n = len({i for f in g['faces'] for i in f[:3]})
            if n < 1000:
                continue
            bow, back = estimate(lod['points'], g['faces'])
            print(f'  {label} part {pi} group {gi} mat{g["material"]}: faces {len(g["faces"])} points {n}'
                  f' + bowtie {bow} + backface {back} = {n + bow + back}'
                  f'{"  ABOVE 65535" if n + bow + back > 0xffff else ""}')


def main(root, source):
    (mk,) = sorted(Path(root).glob('addon/[0-9][0-9]' + lod_overlay.MARKER_SUFFIX))
    cat = mk.with_name(mk.name[:2] + '.cat')
    marker = json.loads(mk.read_text())
    raw = cat.with_suffix('.dat').read_bytes()
    entries = {e['path']: e for e in read_catalogue(cat)}
    assets = lod_overlay.original_assets(bob1.DEFAULT_GAME)[0] if source else None
    print(f'{cat}: groups with >= 1000 points')
    for body in marker['bodies']:
        e = entries[body['member']]
        ladder = bob1.lods(bob1.parse(unpack(bytes(v ^ 0x33 for v in raw[e['offset']:e['offset'] + e['size']]))))
        print(f'{body["name"]} (C = LOD{body["new_lod"]}, source record {body.get("source_record")})')
        report('C', ladder[body['new_lod']])
        if source:
            src = bob1.lods(bob1.parse(assets.get(body['member'])[0]))
            report(f'source LOD{body.get("source_record")}', src[body.get('source_record', len(src) - 1)])


if __name__ == '__main__':
    main(sys.argv[1], '--source' in sys.argv[2:])
