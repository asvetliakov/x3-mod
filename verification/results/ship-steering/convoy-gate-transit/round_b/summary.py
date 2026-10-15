import glob, os, re, sys
# Round B: run.sh's <cast>.txt per cast and time factor over the three seeds (min..max), and their failures.
# usage: python3 -I summary.py [dir] > summary.txt
d = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
num = r'(-?[0-9.]+)'
F = {
    'x12': r'x12 flown ' + num, 'v_min': r'lane v_min ' + num, 'sm_launch': r'small \S+ launched ' + num, 'sm_stops': r'stops ' + num,
    'sm_x12_s': r'x12_s ' + num, 'sm_gap': r'mem_x12 [0-9]+ capsule ' + num, 'mem_sm_x12': r'mem_x12 ' + num,
    'held': r'held ' + num, 'hold_max_s': r'hold_max ' + num, 'D5_edge_hold': r'D\.5 (?:[0-9]+/){6}([0-9]+/[0-9]+)',
    'D5_both': r'D\.5 (?:[0-9]+/){5}([0-9]+)', 'mem_gap': r'members capsule ' + num, 'launched': r'members capsule \S+ launched ([0-9]+)',
    'span': r'\| members capsule \S+ launched \S+ span ' + num, 'p2_not_kept': r'p2 not_kept ' + num, 'p2_nose': r'p2 not_kept \S+ nose ' + num,
    'prof_shift_m': r'shifted [0-9]+ max ' + num, 'prof_us_max': r'clear -?[0-9]+ us ' + num, 'prof_det': r' det ' + num,
    'avoidable': r'obst avoidable ' + num,
}
cols = {'small': ['sm_launch', 'sm_stops', 'sm_x12_s', 'sm_gap', 'mem_sm_x12', 'held', 'hold_max_s', 'D5_edge_hold', 'D5_both', 'x12', 'mem_gap',
                  'launched', 'span'],
        'lane_': ['x12', 'v_min', 'p2_not_kept', 'p2_nose', 'mem_gap', 'launched', 'span', 'prof_shift_m', 'prof_us_max', 'prof_det', 'avoidable'],
        'x12_': ['x12', 'p2_not_kept', 'p2_nose', 'mem_gap', 'launched', 'span'],
        'arriv': ['x12', 'mem_gap', 'launched', 'span']}
for p in sorted(glob.glob(os.path.join(d, '*.txt'))):
    name = os.path.basename(p)[:-4]
    if name in ('lane', 'summary'):
        continue
    cast = re.sub(r'^(parked|shift)_', '', name)
    keys = next((v for k, v in cols.items() if cast.startswith(k)), cols['x12_'])
    rows, fails = {}, []
    for line in open(p):
        m = re.match(r'scenario C \S+ engine paths tf (\d+) seed \d+: (.*)', line)
        if m:
            for k in keys:
                mm = re.search(F[k], m.group(2))
                if mm:
                    rows.setdefault(int(m.group(1)), {}).setdefault(k, []).append(mm.group(1))
        elif re.match(r'\s+\d+ \S+ tf\d+: ', line):
            fails.append(line.strip())
    print('== ' + name)
    for tf in sorted(rows):
        out = []
        for k in keys:
            v = rows[tf].get(k, [])
            if k == 'D5_edge_hold':
                out.append('D5 edge/hold ' + ' '.join(v))
                continue
            fv = [float(x) for x in v]
            out.append('%s %s' % (k, ('%g' % fv[0]) if fv and min(fv) == max(fv) else ('%g..%g' % (min(fv), max(fv))) if fv else '-'))
        print('  tf%-2d %s' % (tf, ', '.join(out)))
    for f in fails:
        print('  FAIL ' + f)
