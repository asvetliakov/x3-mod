"""Run 78 A run297: per F8 burst, every overlay body (addon/05, 06 x3m-lod.json markers) seen in cull_census:
slot, census lod values, lods/thr ladder, s range (s = r*640/D, the value the LOD loop compares with T*f), px range
(px = s * p00 * width/1280 with the frame's camera_state p00 and width 5120), T_pad (marker thresholds[-1]) and s/T_pad,
and the draws of that body's models in the burst by object_context lod. merged = lod == new_lod (1) with the overlay ladder.
Also the global relation lod vs s/T_pad over all overlay rows in all bursts (to measure the effective switch factor f).
usage: overlay_bursts.py LOG ADDON_DIR"""
import re, sys, json, collections
log, addon = sys.argv[1], sys.argv[2]
mk = {}
for s in ('05', '06'):
    for b in json.load(open(f'{addon}/{s}.x3m-lod.json'))['bodies']:
        mk[b['name'].lower().replace('\\', '/')] = (s, b['new_lod'], b['thresholds'])
norm = lambda b: b.lower().replace('\\', '/')
CEN = re.compile(r'^cull_census device=\d+ frame=(\d+) view=(\w+) node=(\w+) model=(\w+) s=(-?\d+) .*? lod=(-?\d+) verdict=(\w+)(?: scope=\w+)?(?: lods=(\S+) thr=(\S+)(?: body=(\S+))?)?')
rawdraw = []; p00 = {}; rows = collections.defaultdict(list); model_body = {}; draws = collections.defaultdict(collections.Counter); ctx = {}
for line in open(log, errors='replace'):
    if line.startswith('cull_census device'):
        m = CEN.match(line)
        if not m or not m.group(10): continue
        f = int(m.group(1)); b = norm(m.group(10)); model_body[m.group(4)] = b
        if b in mk: rows[f].append((b, int(m.group(5)), int(m.group(6)), m.group(8), m.group(9), m.group(7), m.group(2)))
    elif line.startswith('camera_state device=1 frame='):
        m = re.search(r' frame=(\d+) .* p00=([-\d.e]+)', line)
        if m: p00[int(m.group(1))] = float(m.group(2))
    elif line.startswith('object_context '):
        f = re.search(r' frame=(\d+) ', line).group(1); i = re.search(r' index=(\d+)', line).group(1)
        ctx[(f, i)] = (re.search(r' model=(\w+)', line).group(1), int(re.search(r' lod=(\w+)', line).group(1), 16))
    elif line.startswith('draw '):
        f = re.search(r' frame=(\d+) ', line).group(1); i = re.search(r' index=(\d+)', line).group(1)
        rawdraw.append((f, i))
for f, i in rawdraw:  # join after the pass: object_context/cull_census rows may follow the draw row
    model, lod = ctx.get((f, i), ('-', -1)); draws[int(f)][(model_body.get(model, '-'), lod)] += 1
frames = sorted(rows); bursts = []
for f in frames:
    if not bursts or f - bursts[-1][-1] > 1: bursts.append([f])
    else: bursts[-1].append(f)
glob_rel = collections.defaultdict(collections.Counter)
for bf in bursts:
    print(f'== burst frames {bf[0]}..{bf[-1]} p00 {p00.get(bf[0])}')
    per = collections.defaultdict(lambda: {'s': [], 'lod': collections.Counter(), 'lad': set(), 'view': collections.Counter()})
    for f in bf:
        for b, s, lod, lods, thr, verdict, view in rows[f]:
            e = per[b]; e['s'].append(s); e['lod'][lod] += 1; e['lad'].add((lods, thr)); e['view'][view] += 1
            tp = mk[b][2][-1]
            if s > 0: glob_rel[min(int(s / tp * 10), 40)][lod] += 1
    for b, e in sorted(per.items()):
        slot, nl, thr = mk[b]; tp = thr[-1]; s0, s1 = min(e['s']), max(e['s'])
        pxk = (p00.get(bf[0]) or 0) * 5120 / 1280
        dr = collections.Counter()
        for f in bf:
            for (db, lod), c in draws[f].items():
                if db == b: dr[lod] += c
        print(f'slot{slot} {b} rows={len(e["s"])} views={dict(e["view"])} lod={dict(e["lod"])} ladder={sorted(e["lad"])} marker_thr={thr} '
              f's={s0}..{s1} px={s0*pxk:.0f}..{s1*pxk:.0f} T_pad={tp} s/T_pad={s0/tp:.2f}..{s1/tp:.2f} draws_by_lod={dict(dr)} '
              f'{"MERGED" if e["lod"].get(nl) else "full"}')
print('== all overlay census rows in the bursts: s/T_pad bucket (0.1 wide) -> lod counts')
for k in sorted(glob_rel): print(f'  s/T_pad {k/10:.1f}: {dict(glob_rel[k])}')
