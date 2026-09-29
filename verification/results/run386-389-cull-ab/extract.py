"""Run 110 A cull A/B extraction (run386 defaults, run387 lens-flare gain 0, run388 dock cull off, run389 gain 0 + --debug --perf).

usage: python3 extract.py [out.json]   (reads /tmp/x3-bottleX3-run38{5,6,7,8,9}/session-*.log, one pass each, ~3 s)
Plain runs log frame_end only at frames 0 and 3600, so the per-run timing is the 300-frame mean frame time from
media_cue_window qpc deltas (10 MHz QPC; checked against the per-frame frame_end mean on run385/389: qpc mean =
frame_end dt_ms mean + 0.5 ms, the integer truncation). Sector frames are aligned by the first cull_small_props_frame
row (its 300-frame window starts on the sector's first frame, S). Scene identity per window: prop-cull candidates per
frame (cull_small_props_frame draws/300) and lens-flare draws per frame (lens_flare_gain_frame draws/300).
"""
import re, sys, json, glob, statistics as st, collections as C
KV = re.compile(r'(\w+)=(\S+)')
RUNS = {385: 'Run109 reference, flares 0.3, dock off, --debug --perf', 386: 'defaults: dock 12 px, flares 0.3',
        387: '+ lens-flare gain 0 (engine flare cull)', 388: '+ cull-dock-parts 0 (dock off, flares 0.3)',
        389: 'gain 0 + --debug --perf, 3 F8 bursts'}
OUT = sys.argv[1] if len(sys.argv) > 1 else None

def log(r): return sorted(glob.glob(f'/tmp/x3-bottleX3-run{r}/session-*.log'))[0]

def pct(v, q):
    v = sorted(v); return v[min(len(v) - 1, int(q * len(v)))] if v else None

BURST = {1038, 1039, 1040, 1041, 1042, 1043, 1044, 1045, 1742, 1743, 1744, 1745, 1746, 1747, 1748, 1749,
         2940, 2941, 2942, 2943, 2944, 2945, 2946, 2947}

def scan(r):
    R = dict(media=[], flare={}, props=[], fend={}, ft={}, fp={}, lp={}, lfc=[], lfb=[], csp=[], cen=C.defaultdict(list),
             oc=C.defaultdict(list), mk={}, cfg=[], anomalies=C.Counter())
    for l in open(log(r), errors='replace'):
        k = l.split(' ', 1)[0]
        if k == 'media_cue_window': d = dict(KV.findall(l)); R['media'].append((int(d['frame']), int(d['qpc'])))
        elif k == 'lens_flare_gain_frame':
            d = dict(KV.findall(l)); R['flare'][int(d['frame'])] = d
            R['anomalies']['gain_refused_nonzero'] += sum(int(v) for kk, v in d.items() if kk.startswith('refused')) > 0
        elif k == 'cull_small_props_frame': R['props'].append(dict(KV.findall(l)))
        elif k == 'frame_end':
            d = dict(KV.findall(l)); R['fend'][int(d['frame'])] = (int(d['dt_ms']), int(d['draws']), int(d['issued']), d['capture'] == '1')
        elif k == 'frame_timing': d = dict(KV.findall(l)); R['ft'][int(d['frame'])] = d
        elif k == 'frame_phases': d = dict(KV.findall(l)); R['fp'][int(d['frame'])] = d
        elif k == 'loop_phases': d = dict(KV.findall(l)); R['lp'][int(d['frame'])] = d
        elif k == 'lens_flare_cull': R['lfc'].append(dict(KV.findall(l)))
        elif k == 'lens_flare_cull_bodies': R['lfb'].append(dict(KV.findall(l)))
        elif k == 'cull_small_parts_frame': R['csp'].append(dict(KV.findall(l)))
        elif k in ('cull_small_parts', 'cull_small_parts_value', 'lens_flare_gain', 'collide_box_cull'):
            R['cfg'].append(l.strip()[:220])
            if 'status=refused' in l or 'patched=0' in l: R['anomalies'][k + '_refused'] += 1
        elif k in ('cull_census', 'object_context', 'scene_end_marker'):
            m = re.search(r' frame=(\d+)', l)
            if m and int(m.group(1)) in BURST:
                d = dict(KV.findall(l)); f = int(d['frame'])
                if k == 'cull_census': R['cen'][f].append(d)
                elif k == 'object_context': R['oc'][f].append(d)
                else: R['mk'][f] = int(d['draw_index'])
    return R

def windows(R):
    S = int(R['props'][0]['frame']) - 299 if R['props'] else None
    out = []; prev = None
    pw = [(int(p['frame']) - 299, int(p['frame']), int(p['draws']) / 300, int(p['culled']), int(p['kept'])) for p in R['props']]
    for f, q in R['media']:
        if prev:
            a, b = prev[0] + 1, f; ms = (q - prev[1]) / 1e4 / (f - prev[0])
            ov = [(min(b, e) - max(a, s0) + 1, per, cu) for s0, e, per, cu, _ in pw if min(b, e) >= max(a, s0)]
            fl = R['flare'].get(f, {})
            fe = [R['fend'][i] for i in range(a, b + 1) if i in R['fend']]
            w = dict(frames=[a, b], rel=[a - S, b - S] if S is not None else None, mean_ms=round(ms, 2), fps=round(1000 / ms, 1),
                     flare_per_frame=round(int(fl.get('draws', 0)) / 300, 1), gain_skipped=int(fl.get('skipped', 0)),
                     props_per_frame=[round(p, 1) for _, p, _ in ov], props_culled=[c for _, _, c in ov])
            if len(fe) > 100:
                w.update(fe_dt_p50=pct([x[0] for x in fe], .5), fe_dt_p95=pct([x[0] for x in fe], .95),
                         draws_p50=pct([x[1] for x in fe], .5), issued_p50=pct([x[2] for x in fe], .5), captured=sum(x[3] for x in fe))
            out.append(w)
        prev = (f, q)
    return S, out

def phases(R):
    keys_t = ('dt_p50_us', 'dt_p95_us', 'draws_p50', 'gap_draw_p50_us', 'gap_draw_per_draw_us', 'gap_pre_p50_us', 'gap_post_p50_us', 'draw_p50_us')
    keys_p = ('pre_render_p50_us', 'views_p50_us', 'view_setup_p50_us', 'view_submit_p50_us', 'overlays_p50_us', 'begin_scene_p50_us')
    return [dict(frame=f, **{k: R['ft'][f].get(k) for k in keys_t}, **{k: R['fp'].get(f, {}).get(k) for k in keys_p},
                 containers_p50_us=R['lp'].get(f, {}).get('containers_p50_us'), input_p50_us=R['lp'].get(f, {}).get('input_p50_us'),
                 issued_p50=pct([R['fend'][i][2] for i in range(f - 299, f + 1) if i in R['fend'] and not R['fend'][i][3]], .5),
                 captured=sum(1 for i in range(f - 299, f + 1) if i in R['fend'] and R['fend'][i][3]),
                 flare_per_frame=round(int(R['flare'].get(f - 1, {}).get('draws', 0)) / 300, 1))
            for f in sorted(R['ft'])]

def fit(x, y):  # least squares y = a*x + b
    n = len(x); mx = sum(x) / n; my = sum(y) / n
    a = sum((u - mx) * (v - my) for u, v in zip(x, y)) / sum((u - mx) ** 2 for u in x); return round(a * 1e3, 1), round(my - a * mx, 2)

def bursts(R):
    out = []
    for b0 in (1038, 1742, 2940):
        fr = range(b0, b0 + 8); B = dict(frames=[b0, b0 + 7])
        B['draws'] = [R['fend'][f][1] for f in fr]; B['issued'] = [R['fend'][f][2] for f in fr]
        B['census'] = [dict(C.Counter(c['verdict'] for c in R['cen'][f])) for f in fr]
        csp = {int(d['frame']): d for d in R['csp']}
        B['dock_culled_row'] = [int(csp[f]['dock_culled']) for f in fr]; B['small_culled_row'] = [int(csp[f]['culled']) for f in fr]
        B['dock_threshold'] = csp[b0]['dock_threshold']; B['dock_px'] = csp[b0]['dock_px']
        dock = [c for c in R['cen'][b0] if c['verdict'] == 'culled_dock']
        B['dock_culled_f0'] = dict(C.Counter(f"d{int(int(c['d']) / 505e3)}km_s{c['s']}" for c in dock))
        B['dock_culled_s_max'] = max((int(c['s']) for c in dock), default=None)
        B['kept_unnamed_f0'] = [dict(s=int(c['s']), radius=int(c['radius']), d_km=round(int(c['d']) / 505e3, 2), lods=c['lods'],
                                     draws=sum(1 for o in R['oc'][b0] if o['node'] == c['node']))
                                for c in R['cen'][b0] if c['body'] == '-' and c['verdict'] == 'kept']
        vb = C.Counter()
        for f in fr:
            cen = {c['node']: c for c in R['cen'][f]}
            for o in R['oc'][f]:
                c = cen.get(o['node'])
                if c and c['body'].startswith('v\\'):
                    vb[(c['body'], 'post' if int(o['index']) > R['mk'].get(f, 10 ** 9) else 'scene')] += 1
        B['v_bodies_drawn_8f'] = {f'{k[0]} ({k[1]})': n for k, n in vb.items()}
        B['v_verdicts_f0'] = dict(C.Counter(c['verdict'] for c in R['cen'][b0] if c['body'].startswith('v\\')))
        out.append(B)
    return out

rep = dict(runs=RUNS, runs_data={})
for r in RUNS:
    R = scan(r); S, W = windows(R)
    D = dict(sector_start=S, windows=W, frame_end_3600=R['fend'].get(3600), cfg=[c[:120] for c in R['cfg'][:4]], anomalies=dict(R['anomalies']),
             lens_flare_cull=[{k: d.get(k) for k in ('frame', 'status', 'culled', 'total', 'enabled', 'bodies', 'mapped')} for d in R['lfc']],
             lens_flare_cull_bodies=R['lfb'])
    if R['ft']: D['perf_windows'] = phases(R)
    if len(R['fend']) > 100:
        sec = [(f, v) for f, v in R['fend'].items() if v[1] > 100 and not v[3]]
        D['frame_end_uncaptured_sector'] = dict(n=len(sec), dt_p50=pct([v[0] for _, v in sec], .5), dt_p95=pct([v[0] for _, v in sec], .95),
                                                 over_50ms=[(f, v[0], v[1]) for f, v in sorted(sec) if v[0] > 50])
    if r == 389: D['bursts'] = bursts(R)
    rep['runs_data'][r] = D

def compare():
    RD = rep['runs_data']; C_ = {}
    def ph1(r):  # identical post-load scene: every overlapping prop window at 82 candidates/frame, rel start >= 0
        return [w for w in RD[r]['windows'] if w['rel'] and w['rel'][0] >= 0 and w['props_per_frame'] and all(p == 82.0 for p in w['props_per_frame'])]
    def ph2(r):  # carrier view: every overlapping prop window at 102 (386/388/389) or 122 (387) candidates/frame, first 2700 sector frames
        return [w for w in RD[r]['windows'] if w['rel'] and 1000 <= w['rel'][0] < 2700 and w['props_per_frame'] and all(p in (102.0, 122.0) for p in w['props_per_frame'])]
    for r in (385, 386, 387, 388, 389):
        a = ph1(r); b = ph2(r)
        C_[r] = dict(phase1=[(w['rel'], w['mean_ms']) for w in a], phase2=[(w['rel'], w['mean_ms'], w['flare_per_frame']) for w in b])
    pairs = {}
    for r in (387, 388):
        out = []
        for w in ph2(386):
            c = (w['rel'][0] + w['rel'][1]) / 2
            m = min(ph2(r), key=lambda x: abs((x['rel'][0] + x['rel'][1]) / 2 - c), default=None)
            if m and abs((m['rel'][0] + m['rel'][1]) / 2 - c) <= 150:
                out.append(dict(rel386=w['rel'], ms386=w['mean_ms'], rel=m['rel'], ms=m['mean_ms'], delta_ms=round(m['mean_ms'] - w['mean_ms'], 2),
                                fps386=w['fps'], fps=m['fps']))
        pairs[f'{r}_vs_386'] = out
    C_['phase2_pairs'] = pairs
    for r in (385, 389):  # --perf runs: sector windows (issued >= 100) without captured frames; ms, slope in us per draw
        W = [w for w in RD[r]['perf_windows'] if (w['issued_p50'] or 0) >= 100 and w['captured'] == 0]
        nf = [w['issued_p50'] - w['flare_per_frame'] for w in W]
        C_[f'perf_fit_{r}'] = dict(windows=len(W), views_vs_nonflare_issued=fit(nf, [int(w['views_p50_us']) / 1e3 for w in W]),
                                   overlays_ms=[int(w['overlays_p50_us']) / 1e3 for w in W], flare_per_frame=[w['flare_per_frame'] for w in W])
        if r == 385: C_['perf_fit_385']['overlays_vs_flare'] = fit([w['flare_per_frame'] for w in W], [int(w['overlays_p50_us']) / 1e3 for w in W])
    rep['compare'] = C_

compare()
del rep['runs_data'][385]['perf_windows']  # summarised by perf_fit_385; keeps the JSON under 50 KB

def show():
    print(json.dumps(rep['compare'], indent=None)); return
    for r, D in rep['runs_data'].items():
        print(f'== run{r} S={D["sector_start"]} fe3600={D["frame_end_3600"]} anomalies={D["anomalies"]}')
        for w in D['windows']:
            print('  ', w)
        for p in D.get('perf_windows', []): print('   perf', p)
        if 'frame_end_uncaptured_sector' in D: print('   fe', D['frame_end_uncaptured_sector'])
        for x in D['lens_flare_cull'] + D['lens_flare_cull_bodies']: print('   lfc', x)
        for B in D.get('bursts', []): print('   burst', B)
show()
if OUT: json.dump(rep, open(OUT, 'w'), separators=(',', ':'))
