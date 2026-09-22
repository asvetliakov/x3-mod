"""Match coarse (LOD 4, DEFAULT technique) draw textures s0/s1/s2 of frame A against LOD 0 draws' s0/s2/s3
of the same model in frame B (same session; texture ids are session-local). usage: stage_match.py tex_A.txt tex_B.txt"""
import sys
def rows(p):
    for l in open(p):
        if l.startswith('T '):
            f = l.split(); st = {x.split(':')[0]: (x.split(':')[1], x.split(':')[2]) for x in f[5:]}
            yield f[1], f[2], f[3], f[4], st
lod0 = {}
for m, lod, i, ps, st in rows(sys.argv[2]):
    if lod == '00000000': lod0.setdefault(m, []).append((i, ps, st))
for m, lod, i, ps, st in rows(sys.argv[1]):
    if lod != '00000004': continue
    key = (st['s0'][0], st['s1'][0], st['s2'][0])
    hit = [f"{j}:{p}" for j, p, s in lod0.get(m, []) if (s['s0'][0], s['s2'][0], s['s3'][0]) == key]
    print(m, i, ps, 's0/s1/s2=', '/'.join(f"{a}:{b}" for a, b in (st['s0'], st['s1'], st['s2'])), 'lod0_match=', hit or '-')
