#!/usr/bin/env python3
"""Heavy bodies (>= 5 draws/node) at the run250 stand (7357) and busy view (10550):
id -> save-table name -> shipped LOD ladder, selected LOD from object_context, and the
draw saving of a one-group coarse record. Prints numbers and names only.

Usage (from repo root; W = worktree holding tools/analysis/bob1.py):
  PYTHONPATH=$W/tools/analysis:tools/analysis python3 verification/results/run250-draws/stand_bodies.py \
      /tmp/x3-bottleX3-run250/session-20260922-230123-216.log <audit.json from `bob1.py audit --json`>
Screen size s is not logged per node in run250 (no census rows); it is bounded from the
selected LOD k and the ladder: max_{j>k} T_j*f <= s < T_k*f (engine f; run250 lod_scale line
applied=0.5 -> proxy_value=2, i.e. the engine multiplied by f=2).
"""
import json, re, sys, subprocess, os
from collections import defaultdict
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'bob1-format'))
from body_id_names import save_body_names, ROOT  # noqa

LOG, AUDIT = sys.argv[1], sys.argv[2]
F_SESSION = 2.0
FRAMES = (7357, 10550)

def acct(frame):
    rows = []
    for ln in (HERE / f'draw_accounting_{frame}.txt').read_text().splitlines():
        m = re.match(r'([0-9a-f]{8})\s+([0-9a-f]{8})\s+(\d+)\s+(\d+)\s', ln)
        if m:
            rows.append((m[1], int(m[2], 16), int(m[3]), int(m[4])))
    return rows

lods = {}
pat = re.compile(r'^object_context device=1 frame=(\d+) .*?node=([0-9a-f]+) .*?model=([0-9a-f]+) lod=([0-9a-f]+)')
with open(LOG, errors='replace') as fh:
    for ln in fh:
        if ln.startswith('object_context'):
            m = pat.match(ln)
            if m and int(m[1]) in FRAMES:
                lods.setdefault((int(m[1]), m[2]), set()).add(int(m[4], 16))

names = {}
for sv in sorted((ROOT / 'save').glob('*.sav')):
    for i, n in enumerate(save_body_names(sv)):
        names.setdefault(20000 + i, set()).add(n)

audit = {}
for b in json.load(open(AUDIT))['bodies']:
    if ':' not in b.get('member', ''): continue
    stem = b['member'].split(':', 1)[1].rsplit('.', 1)[0].replace('/', '\\').lower()
    c = [n for n in ('single_lod', 'non_monotonic', 'coarse_multi_group') if b.get(n)] + (['shadowed=' + str(b['shadowed'])] if b.get('shadowed') else [])
    audit.setdefault(stem, '+'.join(c) or 'clean_multi_lod')
def ladder(name):
    out = subprocess.run([sys.executable, os.environ['BOB1'], 'info', name], capture_output=True, text=True).stdout
    L = []
    for m in re.finditer(r'LOD(\d+) (?:scale|threshold)=(\d+).*?draws=(\d+) faces=(\d+)', out):
        L.append(dict(i=int(m[1]), T=int(m[2]) if int(m[1]) else None, g=int(m[3]), faces=int(m[4])))
    return L

cache = {}
for frame in FRAMES:
    rows = [r for r in acct(frame) if r[2] >= 5]
    per = defaultdict(list)
    for r in rows: per[r[1]].append(r)
    print(f'== frame {frame}: {len(rows)} heavy nodes, {sum(r[2] for r in rows)} draws')
    tot = 0
    for mid, rs in sorted(per.items(), key=lambda kv: -sum(r[2] for r in kv[1])):
        nm = names.get(mid, {'unmapped'})
        name = sorted(nm)[0]
        if name not in cache:
            cache[name] = ladder(name) if name != 'unmapped' else []
        L = cache[name]
        key = name.replace('/', '\\').lower()
        cls = [audit.get('objects\\' + key)] if ('objects\\' + key) in audit else []
        lad = ' '.join(f"L{l['i']}:T={l['T']},g={l['g']}" for l in L)
        print(f'model {mid:#x} ({mid}) {name}{" [names differ across saves]" if len(nm) > 1 else ""} nLOD={len(L)} ladder[{lad}] audit={cls[0] if cls else "?"}')
        for node, _, d, p in rs:
            ks = lods.get((frame, node), set())
            k = min(ks) if ks else None
            sb = 'n/a'
            if k is not None and L:
                hi = L[k]['T'] * F_SESSION if k >= 1 else None
                lo = max([L[j]['T'] * F_SESSION for j in range(k + 1, len(L))], default=0)
                sb = f'{lo:g}<=s<{hi:g}' if hi else f's>={lo:g}'
                if hi is not None and lo >= hi:
                    sb += ' CONTRADICTS walk rule'
            gk = L[k]['g'] if (k is not None and L) else None
            save = d - 1
            tot += save
            print(f'  node {node} draws={d} prims={p} lod={sorted(ks)} groups@lod={gk} s_bound(f=2)={sb} save_1group={save}')
    print(f'  frame {frame} total saving if every heavy node drew 1 group: {tot}')
