import glob, os, re, sys
# The parked casts' figures per cast and time factor over the three seeds (min..max), from run.sh's <cast>.txt; and their failures.
# usage: python3 -I summary.py [dir] > summary.txt
d = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
num = r'(-?[0-9.]+)'
F = {
    'x12': r'x12 flown ' + num, 'turn': r'turn ' + num + ' deg', 'dur': r'dur ' + num + ' s', 'reissue': r'reissue ' + num,
    'v_min': r'lane v_min ' + num, 'slow': r'slow ' + num + ' s', 'sm_launch': r'launched ' + num + r' mirrored', 'sm_stops': r'stops ' + num,
    'sm_x12_s': r'x12_s ' + num, 'mem_sm_w': r'mem_warns ' + num, 'sm_gap': r'mem_x12 [0-9]+ capsule ' + num,
    'arr_rest': r'rest ' + num, 'exit_v': r'exit_v ' + num, 'hold_out': r'hold_out ' + num, 'dep8_gap': r'dep8_gap ' + num,
    'dep_gap': r'dep_gap ' + num, 'arr_gap': r'arr_gap ' + num, 'mem_gap': r'members capsule ' + num, 'launched': r'members capsule \S+ launched ' + num,
    'span': r'\| members capsule \S+ launched \S+ span ' + num, 'p2_not_kept': r'p2 not_kept ' + num, 'p2_nose': r'p2 not_kept \S+ nose ' + num,
    'D5': r'D\.5 ([0-9/]+)',
}
cols = {'x12_': ['x12', 'turn', 'dur', 'reissue', 'p2_not_kept', 'p2_nose', 'mem_gap', 'launched', 'span'],
        'lane_': ['x12', 'v_min', 'slow', 'p2_not_kept', 'p2_nose', 'mem_gap', 'launched', 'span'],
        'small': ['sm_launch', 'sm_stops', 'sm_x12_s', 'sm_gap', 'mem_sm_w', 'D5', 'x12', 'mem_gap', 'launched', 'span'],
        'arriv': ['x12', 'arr_rest', 'exit_v', 'hold_out', 'arr_gap', 'dep8_gap', 'dep_gap', 'mem_gap', 'launched', 'span', 'slow']}
for p in sorted(glob.glob(os.path.join(d, '*.txt'))):
    cast = os.path.basename(p)[:-4]
    if cast in ('item_a', 'summary'):
        continue
    keys = next(v for k, v in cols.items() if cast.startswith(k))
    rows = {}
    fails = []
    for line in open(p):
        m = re.match(r'scenario C \S+ engine paths tf (\d+) seed \d+: (.*)', line)
        if m:
            tf = int(m.group(1))
            for k in keys:
                mm = re.search(F[k], m.group(2))
                if mm:
                    rows.setdefault(tf, {}).setdefault(k, []).append(mm.group(1))
        elif re.match(r'\s+\d+ \S+ tf\d+: ', line):
            fails.append(line.strip())
    print('== ' + cast)
    for tf in sorted(rows):
        out = []
        for k in keys:
            v = rows[tf].get(k, [])
            if k == 'D5':
                out.append('D5(no_funnel/near/not_arc/clear/mirrored/both) ' + ' '.join(v))
                continue
            fv = [float(x) for x in v]
            out.append('%s %s' % (k, ('%g' % fv[0]) if fv and min(fv) == max(fv) else ('%g..%g' % (min(fv), max(fv))) if fv else '-'))
        print('  tf%-2d %s' % (tf, ', '.join(out)))
    for f in fails:
        print('  FAIL ' + f)
