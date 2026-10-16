#!/usr/bin/env python3
"""Do an object's draws bind the hull light-map programs? Joins the object's object_bounds draw indices (by node) with the
draw rows' ps hash and routed state, and the hull_lightmap_variant / hull_lightmap_widen_variant creation rows (by original
hash). Usage: titan_lightmap.py <log> <node> <frame> [frame ...]"""
import sys, re, subprocess, collections
log, node, frames = sys.argv[1], sys.argv[2], sys.argv[3:]
g = lambda pat: subprocess.run(['grep', '-E', pat, log], capture_output=True, text=True, env={'LC_ALL': 'C'}).stdout.splitlines()
var = {}
for l in g('^(hull_lightmap_variant|hull_lightmap_widen_variant) device=1 '):
    kv = dict(re.findall(r'(\w+)=(\S+)', l)); var.setdefault(kv['original'], {})[l.split()[0]] = kv['create']
wide = [l for l in g('^hull_lightmap_widen_draw ')]
print(f'light-map originals: {len(var)}; gained variant created {sum(v.get("hull_lightmap_variant")=="00000000" for v in var.values())}; '
      f'widened variant created {sum(v.get("hull_lightmap_widen_variant")=="00000000" for v in var.values())} '
      f'(create codes {collections.Counter(v.get("hull_lightmap_widen_variant") for v in var.values())}); hull_lightmap_widen_draw rows in log: {len(wide)}')
for f in frames:
    idx = {dict(re.findall(r'(\w+)=(\S+)', l))['index'] for l in g(f'^object_bounds device=1 frame={f} .*node={node} ')}
    ps = collections.Counter(); routed = collections.Counter()
    for l in g(f'^draw device=1 frame={f} '):
        kv = dict(re.findall(r'(\w+)=(\S+)', l))
        if kv['index'] in idx: ps[kv['ps']] += 1
    for l in g(f'^motion_route device=1 frame={f} '):
        kv = dict(re.findall(r'(\w+)=(\S+)', l))
        if kv['index'] in idx: routed[kv['routed']] += 1
    lm = {h: c for h, c in ps.items() if h in var}
    wd = [dict(re.findall(r'(\w+)=(\S+)', l)) for l in g(f'^hull_lightmap_widen_draw device=1 frame={f} ')]
    wo = [w for w in wd if w['index'] in idx]
    print(f'frame {f}: widened draws of this node {len(wo)} of {len(idx)} (frame total {len(wd)}); c values {sorted({w["c"] for w in wo})}; '
          f'sizes {dict(collections.Counter(w["size"] for w in wo))}; ps {dict(collections.Counter(w["ps"] for w in wo))}')
    fr = dict(re.findall(r'(\w+)=(\S+)', (g(f'^hull_lightmap_widen_frame device=1 frame={f} ') or [''])[0]))
    ff = dict(re.findall(r'(\w+)=(\S+)', (g(f'^hull_lightmap_far_fade_frame device=1 frame={f} ') or [''])[0]))
    print(f'frame {f}: node draws {len(idx)}, routed {dict(routed)}; ps {dict(ps)}; light-map programs among them {lm} '
          f'(gained create {[var[h].get("hull_lightmap_variant") for h in lm]}, widened create {[var[h].get("hull_lightmap_widen_variant") for h in lm]}); '
          f'frame: admitted {fr.get("admitted")} widened {fr.get("widened")} held {fr.get("held")}; far fade faded {ff.get("faded")} min_gain {ff.get("min_gain")}')
