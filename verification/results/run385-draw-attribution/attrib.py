"""Run385 F8 burst draw attribution + LOD state (extends run375-draw-attribution/attrib.py).

usage: python3 attrib.py [session.log] [out.json]
Joins draw / object_context / capture_event(draw_begin qpc) by (frame,index), names each draw by its
node's cull_census body (unnamed '-' nodes take the nearest named ancestor via object_ancestor links),
predicts the drawn LOD from s and thr (f=1.0, Very High -1, clamp) and checks it against lod=.
Single pass over the log; prints a compact report, writes JSON.
"""
import re, sys, json, glob, collections, statistics as st
LOG = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob('/tmp/x3-bottleX3-run385/session-*.log'))[0]
OUT = sys.argv[2] if len(sys.argv) > 2 else None
UPM = 505.0  # world units per metre (inferred, lod-selection.md)
KV = re.compile(r'(\w+)=(\S+)')
FRM = re.compile(r'\bframe=(\d+)')
caps = []; fend = {}; loop = []; ftim = []; fph = []; cspf = []; lsw = []
rows = collections.defaultdict(list)
WANT = {'cull_census', 'object_context', 'draw', 'cull_small_prop', 'cull_small_prop_box', 'scene_end_marker',
        'object_ancestor', 'object_ancestry', 'capture_event', 'cull_small_parts_frame', 'motion_input'}
# pass 1: frame_end (all frames) + periodic rows; capture frames are known from capture=1
for l in open(LOG, errors='replace'):
    k = l.split(' ', 1)[0]
    if k == 'frame_end':
        d = dict(KV.findall(l)); f = int(d['frame'])
        fend[f] = (int(d['dt_ms']), int(d['draws']), int(d.get('issued', -1)), d.get('capture') == '1')
    elif k == 'loop_phases': loop.append(dict(KV.findall(l)))
    elif k == 'frame_timing': ftim.append(dict(KV.findall(l)))
    elif k == 'frame_phases': fph.append(dict(KV.findall(l)))
    elif k == 'cull_small_props_frame': cspf.append(dict(KV.findall(l)))
    elif k == 'lod_switch': lsw.append(dict(KV.findall(l)))
    elif k in WANT:
        m = FRM.search(l)
        if m: rows[(k, int(m.group(1)))].append(l)
capf = sorted(f for f, v in fend.items() if v[3])
bursts = []
for f in capf:
    if bursts and f == bursts[-1][-1] + 1: bursts[-1].append(f)
    else: bursts.append([f])

def p(v, q):
    v = sorted(v); return v[min(len(v) - 1, int(q * len(v)))] if v else None

def nearest(lst, f, key='frame'):
    return min(lst, key=lambda d: abs(int(d[key]) - f)) if lst else None

def classify(body, root, post):
    b = (body or '').replace('/', '\\').lower(); r = (root or '').replace('/', '\\').lower()
    if post:
        if b.startswith('effects\\menugfx') or body is None: return 'hud'
        if b.startswith('v\\'): return 'flare_glow_sprite'
        return 'post_other'
    if b.startswith('ships\\props'): return 'turret_prop'
    if b.startswith('ships\\') : return 'hull' if b.endswith('\\hull') else 'ship_part'
    if b == '-' or b == '':
        if r.startswith('ships\\'): return 'unnamed_ship_child'
        if r.startswith('stations\\'): return 'unnamed_station_child'
        return 'unnamed'
    if b.startswith('stations\\'): return 'station'
    if b.startswith('environments\\nebulae'): return 'sky'
    if b.startswith('environments\\asteroids'): return 'asteroid'
    if b.startswith('effects\\engines'): return 'engine_glow'
    if b.startswith('effects\\'): return 'effect'
    return 'other'

def pred_lod(s, thr):
    n = len(thr); i = 0
    for j in range(n - 1, 0, -1):
        if s < thr[j]: i = j; break
    return max(0, min(n - 1, i - 1))  # Very High: -1, clamp

def effective(r, thr):
    # sweep s downward; each change of the drawn record (Very High rule) gives D = r*640/s at the boundary
    out = []; prev = pred_lod(10 ** 7, thr)
    for s_ in range(max(thr[1:] + [1]) + 1, 0, -1):
        k = pred_lod(s_, thr)
        if k != prev: out.append({'from': prev, 'to': k, 'below_s': s_ + 1, 'beyond_km': km(r * 640 / (s_ + 1))}); prev = k
    return out

def km(u): return round(u / UPM / 1000.0, 2)

def analyse_frame(f):
    cen = {}
    for l in rows[('cull_census', f)]:
        d = dict(KV.findall(l)); cen[d['node']] = d
    anc = collections.defaultdict(dict)
    for l in rows[('object_ancestor', f)]:
        d = dict(KV.findall(l)); anc[d['id']][int(d['link'])] = d['node']   # id -> {link position: node}, 0 = self
    idx2id = {int(d['index']): d['id'] for d in (dict(KV.findall(l)) for l in rows[('object_ancestry', f)])}
    mk = int(dict(KV.findall(rows[('scene_end_marker', f)][0]))['draw_index'])
    oc = {int(d['index']): d for d in (dict(KV.findall(l)) for l in rows[('object_context', f)])}
    dr = {int(d['index']): d for d in (dict(KV.findall(l)) for l in rows[('draw', f)])}
    begin = {}
    for l in rows[('capture_event', f)]:
        if 'op=draw_begin' in l:
            d = dict(KV.findall(l)); begin[int(d['after_draw']) + 1] = int(d['qpc'])
    skipped = {int(dict(KV.findall(l))['index']) for l in rows[('cull_small_prop', f)]}
    mi = {int(d['index']): d for d in (dict(KV.findall(l)) for l in rows[('motion_input', f)])}
    chain = {}   # ancestry rows exist only for a node's first draw in the frame
    for i in sorted(oc):
        cid = idx2id.get(i)
        if cid in anc: chain.setdefault(oc[i]['node'], [anc[cid][j] for j in sorted(anc[cid])])
    owner = {}   # scene-graph root -> the named hull/station body drawn under that root
    for i in sorted(oc):
        ch = chain.get(oc[i]['node']); b = cen.get(oc[i]['node'], {}).get('body', '')
        if ch and (b.lower().endswith('\\hull') or b.lower().startswith('stations\\')): owner.setdefault(ch[-1], b)
    draws = []
    for i in sorted(dr):
        o = oc.get(i, {}); node = o.get('node'); c = cen.get(node)
        body = c.get('body') if c else None
        ch = chain.get(node); root = owner.get(ch[-1]) if ch else None
        m = mi.get(i, {})
        gap = begin[i] - begin[i - 1] if (i in begin and i - 1 in begin) else None
        draws.append(dict(i=i, node=node, body=body, root=root, post=i > mk, skipped=i in skipped,
                          cls=classify(body, root, i > mk), gap=gap, prims=int(dr[i].get('primitives', 0)),
                          dup=(node, dr[i].get('vs'), dr[i].get('ps'), dr[i].get('primitives'), m.get('vb'), m.get('ib'), m.get('declaration'))))
    return cen, draws, mk

report = {'log': LOG.split('/')[-1], 'upm_inferred': UPM, 'bursts': []}
for bi, fr in enumerate(bursts):
    f0 = fr[0]
    B = {'frames': [fr[0], fr[-1]]}
    B['captured_dt_ms'] = [fend[f][0] for f in fr]
    B['draws'] = [fend[f][1] for f in fr]; B['issued'] = [fend[f][2] for f in fr]
    pre = [f for f in range(f0 - 120, f0) if f in fend and not fend[f][3]]
    post = [f for f in range(fr[-1] + 1, fr[-1] + 121) if f in fend and not fend[f][3]]
    B['uncaptured_before'] = {'frames': [pre[0], pre[-1]], 'dt_p50_ms': p([fend[f][0] for f in pre], .5),
                              'dt_p95_ms': p([fend[f][0] for f in pre], .95), 'draws_p50': p([fend[f][1] for f in pre], .5),
                              'issued_p50': p([fend[f][2] for f in pre], .5)}
    B['uncaptured_after'] = {'frames': [post[0], post[-1]], 'dt_p50_ms': p([fend[f][0] for f in post], .5),
                             'draws_p50': p([fend[f][1] for f in post], .5), 'issued_p50': p([fend[f][2] for f in post], .5)}
    def win(lst):  # the 300-frame window before the burst and the one containing it
        a = max((d for d in lst if int(d['frame']) < f0), key=lambda d: int(d['frame']))
        b = min((d for d in lst if int(d['frame']) >= fr[-1]), key=lambda d: int(d['frame']))
        return a, b
    B['loop_phases'] = [{k: lp[k] for k in ('frame', 'input_p50_us', 'region_p50_us', 'containers_p50_us', 'cutevent_p50_us', 'collide_p50_us', 'passb_p50_us')} for lp in win(loop)]
    B['frame_timing'] = [{k: ft[k] for k in ('frame', 'dt_p50_us', 'dt_p95_us', 'draws_p50', 'gap_pre_p50_us', 'gap_draw_p50_us', 'gap_draw_per_draw_us')} for ft in win(ftim)]
    B['frame_phases'] = [{k: ph[k] for k in ('frame', 'dt_p50_us', 'pre_render_p50_us', 'views_p50_us', 'view_submit_p50_us', 'scene_update_p50_us')} for ph in win(fph)]
    cs = nearest(cspf, f0); B['cull_small_props_frame'] = {k: cs[k] for k in ('frame', 'draws', 'culled', 'kept', 'kept_size', 'exempt_target', 'no_scope', 'no_bounds')}
    # per-frame class counts (all frames of the burst)
    perframe = []; gaps = collections.Counter(); gapn = collections.Counter(); gl = collections.defaultdict(list)
    for f in fr:
        cen, draws, mk = analyse_frame(f)
        c = collections.Counter(d['cls'] + ('/skipped' if d['skipped'] else '') for d in draws)
        perframe.append(dict(frame=f, marker=mk, n=len(draws), cls=dict(c),
                             census=dict(collections.Counter(x['verdict'] for x in cen.values()))))
        for d in draws:
            if d['gap'] is not None: gaps[d['cls']] += d['gap']; gapn[d['cls']] += 1; gl[d['cls']].append(d['gap'])
    B['per_frame'] = perframe
    tot = sum(gaps.values())
    B['draw_begin_gap_by_class'] = {k: {'draws': gapn[k], 'p50_us': round(p(gl[k], .5) / 10, 1), 'sum_ms_per_frame': round(gaps[k] / 1e4 / len(fr), 2),
                                        'share': round(gaps[k] / tot, 3), 'us_per_draw': round(gaps[k] / 10 / gapn[k], 1)}
                                    for k in sorted(gaps, key=lambda k: -gaps[k])}
    # frame f0 detail
    cen, draws, mk = analyse_frame(f0)
    per_body = collections.defaultdict(lambda: {'draws': 0, 'nodes': set(), 'skipped': 0, 'cls': None})
    for d in draws:
        key = d['body'] if d['body'] not in (None, '-') else ('- (unnamed part under ' + (d['root'] or '?') + ')' if d['body'] == '-' else ('<no census> ' + ('post' if d['post'] else 'scene')))
        e = per_body[key]; e['draws'] += 1; e['nodes'].add(d['node']); e['cls'] = d['cls']; e['skipped'] += d['skipped']
    top = []
    for key, e in sorted(per_body.items(), key=lambda kv: -kv[1]['draws']):
        cr = [cen[n] for n in e['nodes'] if n in cen]
        top.append({'body': key, 'cls': e['cls'], 'draws': e['draws'], 'skipped': e['skipped'], 'nodes': len(e['nodes']),
                    'lod': sorted({c['lod'] for c in cr}), 'd_km': sorted({km(int(c['d'])) for c in cr})[:4],
                    'radius': sorted({int(c['radius']) for c in cr})[:3], 's': sorted({int(c['s']) for c in cr})[:4],
                    'thr': sorted({c['thr'] for c in cr})[:2]})
    B['frame0_bodies'] = top[:25]
    B['frame0_single_record_draws'] = dict(collections.Counter(d['cls'] for d in draws if not d['post'] and not d['skipped']
                                                               and cen.get(d['node'], {}).get('lods') == '1'))
    B['frame0_dup_draws'] = sum(v - 1 for v in collections.Counter(d['dup'] for d in draws if d['node']).values() if v > 1)
    ow = collections.defaultdict(collections.Counter)
    for d in draws:
        if d['root'] and not d['post']: ow[d['root']][d['cls']] += 1
    B['frame0_per_owner'] = {k: dict(v) for k, v in sorted(ow.items(), key=lambda kv: -sum(kv[1].values()))}
    un = [c for c in cen.values() if c.get('body') == '-']
    B['unnamed_census'] = {'verdicts': dict(collections.Counter(c['verdict'] for c in un)),
                           'lods': dict(collections.Counter(c['lods'] for c in un if c['verdict'] == 'kept'))}
    # LOD state of hull bodies (census, any verdict, main view)
    hull = []; mism = []
    for n, c in cen.items():
        b = c.get('body', '')
        if not b.lower().startswith('ships\\') or b.lower().startswith('ships\\props'): continue
        r = int(c['radius']); s = int(c['s']); D = int(c['d'])
        thr = [int(t) for t in c['thr'].split(',')] if c['thr'] != '-' else []
        row = {'body': b, 'node': n, 'verdict': c['verdict'], 'lod': int(c['lod']), 'lods': c['lods'], 'thr': c['thr'],
               's': s, 'd_km': km(D), 'radius': r, 'view': c['view'], 'drawn': sum(1 for d in draws if d['node'] == n and not d['skipped'])}
        if thr and c['verdict'] == 'kept':
            row['pred_lod'] = pred_lod(s, thr)
            row['switch_km_raw'] = [km(r * 640 / t) for t in thr[1:]]            # D_i = r*640/T_i (loop boundary i)
            row['effective_km'] = effective(r, thr)
            if row['pred_lod'] != row['lod']: mism.append(row)
        hull.append(row)
    hull.sort(key=lambda r: (r['body'], r['d_km']))
    B['hull_rows_not_kept'] = dict(collections.Counter(h['verdict'] for h in hull if h['verdict'] != 'kept'))
    hull = [h for h in hull if h['verdict'] == 'kept']
    B['hull_lod'] = hull; B['lod_mismatch'] = len(mism)
    # all kept census rows (any class): predicted vs recorded LOD agreement
    agree = tot_k = 0
    for c in cen.values():
        if c['verdict'] == 'kept' and c['thr'] != '-':
            thr = [int(t) for t in c['thr'].split(',')]
            if len(thr) >= 1 and int(c['lods']) == len(thr):
                tot_k += 1; agree += pred_lod(int(c['s']), thr) == int(c['lod'])
    B['lod_rule_agreement'] = [agree, tot_k]
    # props
    pv = collections.Counter(c['verdict'] for c in cen.values() if c.get('body', '').lower().startswith('ships\\props'))
    B['prop_census_verdicts'] = dict(pv)
    box = [dict(KV.findall(l)) for l in rows[('cull_small_prop_box', f0)]]
    kb = [(cen.get(b['node'], {}).get('body'), float(b['extent_px'])) for b in box if b['verdict'].startswith('kept')]
    B['prop_box_kept'] = {'rows': len(kb), 'extent_px_min': min((x for _, x in kb), default=None),
                          'extent_px_p50': p([x for _, x in kb], .5), 'extent_px_max': max((x for _, x in kb), default=None),
                          'verdicts': dict(collections.Counter(b['verdict'] for b in box)),
                          'bodies': dict(collections.Counter(b for b, _ in kb))}
    B['prop_skipped_bodies'] = dict(collections.Counter(d['body'] for d in draws if d['skipped']))
    B['lod_switch_near'] = len([x for x in lsw if abs(int(x['frame']) - f0) <= 300])
    report['bursts'].append(B)

def show(B):
    print('burst', B['frames'], 'draws', B['draws'], 'issued', B['issued'], 'captured_dt', B['captured_dt_ms'])
    for k in ('uncaptured_before', 'uncaptured_after', 'loop_phases', 'frame_timing', 'frame_phases', 'cull_small_props_frame'):
        print(' ', k, B[k])
    for pf in B['per_frame'][:1]: print('  f0 classes', pf)
    print('  class counts per frame:', [pf['cls'] for pf in B['per_frame']][1:2])
    print('  gap_by_class', {k: (v['draws'], v['p50_us'], v['share']) for k, v in B['draw_begin_gap_by_class'].items()})
    print('  top bodies:')
    for t in B['frame0_bodies'][:18]: print('   ', t)
    print('  single-record draws', B['frame0_single_record_draws']); print('  dup', B['frame0_dup_draws'], 'unnamed', B['unnamed_census']); print('  owners', B['frame0_per_owner'])
    print('  lod rule agreement', B['lod_rule_agreement'], 'hull mismatches', B['lod_mismatch'])
    for h in B['hull_lod']:
        if h['verdict'] == 'kept': print('   H', {k: h[k] for k in h if k not in ('node', 'view')})
    print('   hull rows not kept:', B['hull_rows_not_kept'])
    print('  props census', B['prop_census_verdicts'], 'skipped', B['prop_skipped_bodies'])
    print('  prop box kept', B['prop_box_kept'])
for B in report['bursts']: show(B)
if OUT:
    for B in report['bursts']:
        B['per_frame_classes'] = [pf['cls'] for pf in B['per_frame']]
        B['per_frame_census'] = B['per_frame'][0]['census']; B['markers'] = [pf['marker'] for pf in B['per_frame']]
        del B['per_frame']
    json.dump(report, open(OUT, 'w'), separators=(',', ':'), default=list)
