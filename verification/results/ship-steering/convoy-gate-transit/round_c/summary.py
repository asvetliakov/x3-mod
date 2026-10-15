import glob, os, re, sys
# Round C: run.sh's <cast>.txt per cast and time factor over the three seeds (min..max), and their failures. before_<cast>: d2871285.
# usage: python3 -I summary.py [dir] > summary.txt
d = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
num = r'(-?[0-9.]+)'
F = {
    'x12': r'x12 flown ' + num, 'turn': r'turn ' + num, 'dur_s': r' dur ' + num, 'v_min': r'lane v_min ' + num, 'slow_s': r'slow ' + num,
    'mem_gap': r'members capsule ' + num, 'launched': r'members capsule \S+ launched ([0-9]+)',
    'span': r'\| members capsule \S+ launched \S+ span ' + num, 'p2_not_kept': r'p2 not_kept ' + num, 'p2_nose': r'p2 not_kept \S+ nose ' + num,
    'loops': r'\| loop ' + num, 'loop_min_s': r'\| loop [0-9]+ min_s ' + num, 'loop_bound': r' bound ' + num + r' turn_lat',
    'turn_lat': r'turn_lat ' + num, 'fol_v_min': r'fol_v_min ' + num, 'sm_gap': r'mem_x12 [0-9]+ capsule ' + num,
    'arr_overlap': r'overlap ' + num, 'avoidable': r'obst avoidable ' + num,
}
cols = {'x12_': ['x12', 'turn', 'dur_s', 'p2_not_kept', 'p2_nose', 'v_min', 'slow_s', 'fol_v_min', 'mem_gap', 'launched', 'span', 'loops',
                 'loop_min_s', 'loop_bound', 'turn_lat'],
        'lane_': ['x12', 'v_min', 'slow_s', 'p2_not_kept', 'p2_nose', 'mem_gap', 'launched', 'span', 'avoidable', 'loops'],
        'arriv': ['x12', 'mem_gap', 'arr_overlap', 'launched', 'span', 'loops']}
for p in sorted(glob.glob(os.path.join(d, '*.txt'))):
    name = os.path.basename(p)[:-4]
    if name in ('lane', 'summary', 'bench'):
        continue
    cast = re.sub(r'^(parked|before)_', '', name)
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
            fv = [float(x) for x in rows[tf].get(k, [])]
            out.append('%s %s' % (k, ('%g' % fv[0]) if fv and min(fv) == max(fv) else ('%g..%g' % (min(fv), max(fv))) if fv else '-'))
        print('  tf%-2d %s' % (tf, ', '.join(out)))
    for f in fails:
        print('  FAIL ' + f)
