#!/usr/bin/env python3
"""Mod-body facts for the merged-LOD census on a synthetic mod root (read-only).

  PYTHONPATH=tools/analysis python3 mod_bodies.py MODROOT VANROOT CENSUS_TXT [VANILLA_SUMMARY]
MODROOT: bottle root 01..13 + addon 01..04 + mod catalogues (addon 05..12) + loose dirs.
VANROOT: bottle root 01..13 + addon 01..04 (no overlay 05). Prints counts only."""
import re, sys
from collections import Counter
from pathlib import Path, PurePosixPath
import sector_fog_census as sfc, bob1, body_materials

mod, van = sfc.Assets(Path(sys.argv[1])), sfc.Assets(Path(sys.argv[2]))
MODCATS = {f'addon/{n:02d}.cat' for n in range(5, 13)}

def norm(name):
    s = name.replace('\\', '/').lower().strip('/')
    s = s.removeprefix('addon/').removeprefix('objects/')
    return re.sub(r'\.(pbb|pbd|bob|bod)$', '', s)

def src(e): return e['source'] if not e['source'].startswith('loose:') else 'loose'

# winning .pbb as bob1.audit does
win = {}
for k, v in mod.entries.items():
    if v[-1]['path'].lower().endswith(('.pbb', '.bob')):
        win[norm(v[-1]['path'])] = v[-1]
kinds = {}
modb = {}
for n, e in win.items():
    if src(e) in MODCATS or src(e) == 'loose':
        d = mod.read_entry(e); mod.cache.clear()
        kinds[n] = bob1.kind(d)
        if kinds[n] == 'BOB1':
            modb[n] = e
cen = {}
for line in open(sys.argv[3]):
    f = line.split()
    if not f: continue
    kv = dict(x.split('=', 1) for x in f[1:] if '=' in x)
    kv['line'] = line
    cen[norm(f[0])] = kv
print('winning pbb stems', len(win), 'from mod cats (any kind)', sum(1 for n in kinds),
      'CUT1', sum(1 for k in kinds.values() if k == 'CUT1'), 'BOB1', len(modb))
print('mod BOB1 by catalogue', dict(sorted(Counter(src(e) for e in modb.values()).items())))
def vhas(n): return bool(van.candidates('objects/' + n + '.pbb') or van.candidates('objects/' + n + '.pbd'))
cats = Counter(); nov = Counter(); miss = 0
for n in modb:
    c = cen.get(n, {}).get('cat')
    if c is None: miss += 1; c = '?'
    cats[c] += 1; nov[(c, 'override' if vhas(n) else 'new')] += 1
print('Q1 category', dict(cats), 'not in census', miss); print('Q1 new/override', dict(sorted(nov.items())))
print('Q1 by catalogue x category', dict(sorted(Counter((src(e), cen.get(n, {}).get('cat')) for n, e in modb.items()).items())))

# Q2 references: types path fields -> scenes -> bodies (string scan, recursive)
def resolve(n):
    for ext in ('.pbb', '.pbd'):
        c = mod.candidates('objects/' + n + ext)
        if c: return c[-1]
    return None
STR = re.compile(rb'[A-Za-z0-9_\\/.\-]{4,}')
refs, seen, missing, types_src = set(), set(), set(), {}
SCENE = re.compile(rb'^\s*P\s+-?\d+\s*;', re.M)
TXTREF = re.compile(rb'\bB\s+([A-Za-z][A-Za-z0-9_\\/\-]*)\s*;')
BINREF = re.compile(rb'[A-Za-z][A-Za-z0-9_\-]*(?:\\[A-Za-z0-9_\-]+)+')
def walk(n, depth=0):
    if n in seen or depth > 8: return
    seen.add(n)
    e = resolve(n)
    if e is None: missing.add(n); return
    d = mod.read_entry(e); mod.cache.clear()
    k = bob1.kind(d)
    if k == 'BOB1' or (k is None and not SCENE.search(d)):
        refs.add(n); return
    for s in (BINREF.findall(d) if k == 'CUT1' else TXTREF.findall(d)):
        walk(norm(s.decode()), depth + 1)
typerefs = Counter()
for t in ('TShips', 'TDocks', 'TFactories', 'TSpecial', 'TShields', 'TGates', 'TPlanets', 'TSun', 'TAsteroids', 'TLaser', 'TMissiles', 'TWareT', 'TBackgrounds', 'TCockpits'):
    for ext in ('.txt', '.pck'):
        c = mod.candidates('types/' + t + ext)
        if c: break
    if not c: continue
    types_src[t] = src(c[-1])
    txt = mod.read_entry(c[-1]).decode('latin1')
    for fld in re.split(r'[;\r\n]', txt):
        if '\\' in fld and re.match(r'^[A-Za-z0-9_\\\-\.]+$', fld):
            typerefs[t] += 1; walk(norm(fld))
print('types winners', types_src, 'path fields', dict(typerefs))
# direct-body check: BOB1 bodies reached
reached = refs & set(modb)
print('Q2 mod BOB1 referenced', len(reached), 'unreferenced', len(modb) - len(reached),
      'by cat', dict(Counter(cen.get(n, {}).get('cat') for n in reached)))
print('Q2 unreferenced by cat', dict(Counter(cen.get(n, {}).get('cat') for n in set(modb) - reached)))
print('Q2 bodies reached (any source)', len(refs), 'of which mod-catalogue winners', sum(1 for n in refs if (resolve(n) or {}).get('source') in MODCATS), 'text bodies (not censusable)', sum(1 for n in refs if n not in win))
missing_pathlike = sorted(missing)
print('Q2 referenced names with no pbb/pbd anywhere', len(missing_pathlike), missing_pathlike[:15])

# Q3
lods = Counter(); t1 = Counter(); big = 0; one = 0
for n in modb:
    kv = cen.get(n, {})
    if 'lods' not in kv: continue
    lods[int(kv['lods'])] += 1; one += kv['lods'] == '1'
    thr = kv.get('thr', '-')
    t1[thr.strip('[]').split(',')[0] if thr != '-' else '-'] += 1
    big += int(kv.get('r0_points', 0)) > 60000
print('Q3 record counts', dict(sorted(lods.items())), 'single', one, 'r0_points>60000', big)
print('Q3 T1 distribution', dict(sorted(t1.items(), key=lambda x: -x[1])))
print('Q3 census parse failures among mod bodies', sum(1 for n in modb if 'lods' not in cen.get(n, {})),
      dict(Counter(cen.get(n, {}).get('refuse') for n in modb if 'lods' not in cen.get(n, {}))))

# Q4
elig = [n for n in modb if 'ELIGIBLE' in cen.get(n, {}).get('line', '')]
print('Q4 eligible', len(elig), dict(Counter(cen[n]['cat'] for n in elig)))
rc = Counter(); rcs = Counter(); other = Counter(); mat = Counter()
for n in modb:
    kv = cen.get(n, {}); mat[kv.get('mat')] += 1
    for r in kv.get('refuse', '-').split(','):
        if r != '-': rc[r] += 1; rcs[(kv['cat'], r)] += 1
    if any(r in kv.get('refuse', '') for r in ('atlas_other', 'exception', 'parse_error', 'dominant_slot_missing')):
        m = re.search(r'error="(.*?)"', kv['line']) or re.search(r'error=(.*)', kv['line'])
        other[re.sub(r"b'[^']*'|\d+", 'X', m.group(1))[:90] if m else '?'] += 1
print('Q4 refusals', dict(rc.most_common())); print('Q4 refusals ship/station', {k: v for k, v in sorted(rcs.items()) if k[0] != 'other'})
print('Q4 material kinds', dict(mat)); print('Q4 atlas_other/exception texts', dict(other.most_common(12)))
exts = Counter(); unread = Counter(); unread_mats = 0; mats_total = 0; kind_ok = Counter()
for n, e in modb.items():
    try: tree = bob1.parse(mod.read_entry(e))
    except bob1.FormatError: continue
    finally: mod.cache.clear()
    for m in bob1.materials(tree):
        mats_total += 1; bad = False
        for slot, name in body_materials.slots(m).items():
            if body_materials.is_null(name) or name.isdigit(): continue
            p = PurePosixPath(name.decode('latin1').replace('\\', '/'))
            exts[p.suffix.lower() or '(none)'] += 1
            try:
                d, _ = mod.logical('dds/' + p.stem, ('.pck', '.dds', '.tga'))
            except ValueError: d = None
            if d is None:
                bad = True; unread[p.suffix.lower() or '(none)'] += 1
        unread_mats += bad
print('Q4 texture name extensions in mod-body materials', dict(exts.most_common()))
print('Q4 texture refs unreadable by lod_atlas lookup by ext', dict(unread), 'materials with >=1 unreadable', unread_mats, 'of', mats_total)
tex = Counter()
for k, v in mod.entries.items():
    e = v[-1]
    if src(e) in MODCATS or src(e) == 'loose':
        top = k.removeprefix('addon/').split('/')[0]
        if top in ('dds', 'tex') or k.endswith(('.dds', '.jpg', '.tga', '.pck')) and '/' in k:
            tex[(top, PurePosixPath(e['path']).suffix.lower())] += 1
print('Q4 texture-ish members winning from mod cats (top dir, ext)', dict(tex.most_common(12)))

# Q5 collisions
names = [e['path'] for v in mod.entries.values() for e in v if src(e) in MODCATS]
print('Q5 mod members matching x3m_lod', sum('x3m_lod' in p.lower() for p in names), 'addon/mods paths', sum(p.lower().startswith(('mods/', 'addon/mods/')) for p in names))

# Q6
def mb(s): m = re.search(r'atlas@1280=(\d+)\(ratio [\d.]+, ([\d.]+) MB\)', s); return (int(m.group(1)), float(m.group(2))) if m else (0, 0)
tot = cap = 0; sizes = Counter()
for n in elig:
    side, v = mb(cen[n]['line']); tot += v; sizes[side] += 1
    cap += v if side <= 2048 else v * (2048 / side) ** 2
print(f'Q6 mod eligible atlas@1280 {tot:.2f} MB, capped-2048 ~{cap:.2f} MB (inferred /4 per step), sizes {dict(sizes)}')
if len(sys.argv) > 4:
    for l in open(sys.argv[4]):
        if l.startswith(('bodies', 'eligible')) or 'eligible totals at 1280' in l: print('vanilla summary:', l.strip()[:200])
