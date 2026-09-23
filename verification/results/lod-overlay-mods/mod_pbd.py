#!/usr/bin/env python3
"""Mod .pbd (text) members: body vs scene, category, shadowing of vanilla .pbb; eligible-list diff.
  PYTHONPATH=tools/analysis python3 mod_pbd.py MODROOT VANROOT MOD_ELIGIBLE VANILLA_ELIGIBLE"""
import sys, re
from collections import Counter
from pathlib import Path
import sector_fog_census as sfc
mod, van = sfc.Assets(Path(sys.argv[1])), sfc.Assets(Path(sys.argv[2]))
MODCATS = {f'addon/{n:02d}.cat' for n in range(5, 13)}
def cat(k):
    s = k.removeprefix('addon/').removeprefix('objects/')
    top = s.split('/')[0]
    return 'ship' if top == 'ships' else 'station' if top in ('stations', 'others') else 'other'
c = Counter(); shadow = Counter(); ex = []
for k, v in mod.entries.items():
    e = v[-1]
    if not k.endswith('.bod') or e['source'] not in MODCATS: continue
    d = mod.read_entry(e); mod.cache.clear()
    txt = d[:20000].decode('latin1', 'replace')
    kind = 'scene' if re.search(r'^\s*P\s+-?\d+\s*;', txt, re.M) or re.search(r'^\s*[Bb]\s+[A-Za-z]', txt, re.M) or '_scene' in k else 'body'
    stem = k[:-4]
    vb = bool(van.candidates(stem + '.bob')); vd = bool(van.candidates(stem + '.bod'))
    c[(e['source'], cat(k), kind)] += 1
    shadow[(cat(k), kind, 'vanilla_pbb' if vb else 'vanilla_pbd' if vd else 'new')] += 1
    mb = mod.candidates(stem + '.bob')
    if kind == 'body' and mb and len(ex) < 5: ex.append((k, mb[-1]['source']))
print('mod .pbd winners by (cat file, category, kind)', dict(sorted(c.items())))
print('mod .pbd vs vanilla', dict(sorted(shadow.items())))
print('mod text body stems that also have a winning .pbb (census reads the .pbb)', ex)
me = {l.split('=')[0] for l in open(sys.argv[3]) if l.strip()}; ve = {l.split('=')[0] for l in open(sys.argv[4]) if l.strip()}
print('eligible mod-root', len(me), 'vanilla', len(ve), 'lost', len(ve - me), sorted(ve - me)[:20], 'gained', sorted(me - ve)[:20])
# stems where a mod .pbd coexists with a winning .pbb (census and lod_overlay read the .pbb)
co = Counter(); srcs = Counter()
for k, v in mod.entries.items():
    if k.endswith('.bod') and v[-1]['source'] in MODCATS:
        b = mod.candidates(k[:-4] + '.bob')
        if b:
            co[cat(k)] += 1; srcs[(v[-1]['source'], b[-1]['source'])] += 1
print('mod .pbd shadowing a winning .pbb, by category', dict(co), 'by (pbd cat, pbb cat)', dict(srcs))
# which tex sources changed for lost eligibles: mod dds members overriding vanilla dds
ov = sum(1 for k, v in mod.entries.items() if k.startswith('addon/dds/') or k.startswith('dds/') if v[-1]['source'] in MODCATS and van.candidates(k))
print('mod dds members overriding a vanilla dds name', ov)
