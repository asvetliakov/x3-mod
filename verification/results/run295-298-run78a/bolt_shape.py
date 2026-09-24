"""run298 bolt footprint refused_shape triage (read-only over local session logs).
Prints: every bolt_footprint_refused row (runs 295-298, 287); per bolt window with draws>0: frame range, draws, written,
untouched, refused_shape/buffer, chase-hist instances, instances per planned draw (written+untouched), draw accounting
check, the window's chase_fire events and the per-frame additive-admitted max; scene rows (sector, loading) in 9000-11000.
usage: bolt_shape.py [RUN...]  (default 298 287 295 296 297)"""
import glob, re, sys, collections
runs = sys.argv[1:] or ['298', '287', '295', '296', '297']
kv = lambda l: dict(re.findall(r'(\w+)=([^ \n]+)', l))
for run in runs:
    logs = sorted(glob.glob(f'/tmp/x3-bottleX3-run{run}/session-*.log'))
    if not logs: print(f'run{run} no log'); continue
    refusals, wins, hist, fire, scene = [], [], {}, {}, []
    add_max = collections.defaultdict(int); frame = 0
    for l in open(logs[0], errors='replace'):
        k = l.split(' ', 1)[0]
        m = re.search(r' frame=(\d+)', l)
        if m: frame = int(m.group(1))
        if k == 'bolt_footprint_refused': refusals.append(l.strip())
        elif k == 'bolt_footprint': wins.append(kv(l))
        elif k == 'bolt_footprint_hist': d = kv(l); hist[int(d['frame'])] = d
        elif k == 'chase_fire_window': d = kv(l); fire[int(d['frame']) - 1] = d.get('native_inactive_override')
        elif k == 'screen_emission_additive_frame': add_max[frame // 300] = max(add_max[frame // 300], int(kv(l)['admitted']))
        elif k in ('volumetric_fog_sector', 'loading_phase') and 9000 <= frame <= 11000: scene.append(l.strip()[:200])
    print(f'== run{run} refusal rows={len(refusals)}')
    for r in refusals: print('  ', r)
    print('  window(frames)        draws written untouched ref_shape ref_buffer instances inst/planned acct_ok fire add_max_per_frame')
    for d in wins:
        if int(d['draws']) == 0: continue
        f = int(d['frame']); planned = int(d['written']) + int(d['untouched'])
        refused = sum(int(d[x]) for x in d if x.startswith('refused_') and x != 'refused_w')
        ok = planned + int(d['gated']) + refused + int(d['failures']) == int(d['draws'])
        ipd = int(d['instances']) / planned if planned else 0
        print(f"  {f-299:>6}-{f:<6} {d['draws']:>12} {d['written']:>7} {d['untouched']:>9} {d['refused_shape']:>9} {d['refused_buffer']:>10}"
              f" {d['instances']:>9} {ipd:>12.1f} {str(ok):>7} {fire.get(f, '-'):>4} {add_max[(f - 299) // 300]:>4}")
    for s in scene: print('  scene:', s)
