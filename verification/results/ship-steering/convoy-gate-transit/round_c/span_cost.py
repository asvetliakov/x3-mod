import os, re, sys
# Round C: the loop's span cost per run, x12_<cast>.txt against before_x12_<cast>.txt (d2871285), paired by time factor and seed.
# usage: python3 -I span_cost.py [dir] > span_cost.txt
d = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
rx = re.compile(r'scenario C (\S+) engine paths tf (\d+) seed (\d+): .*\| members capsule \S+ launched \S+ span (-?[0-9.]+)')
for cast in ('x12_near', 'x12_far'):
    runs = {}
    for tag, f in (('before', 'before_' + cast), ('after', cast)):
        for line in open(os.path.join(d, f + '.txt')):
            m = rx.match(line)
            if m:
                runs.setdefault((int(m.group(2)), int(m.group(3))), {})[tag] = float(m.group(4))
    diffs = []
    print('== ' + cast + ' (span s, 15 km -> last launch: d2871285 -> round C)')
    for (tf, seed), v in sorted(runs.items()):
        if 'before' in v and 'after' in v:
            diffs.append(v['after'] - v['before'])
            print('  tf%-2d seed %-7d %6.1f -> %6.1f  %+6.1f' % (tf, seed, v['before'], v['after'], diffs[-1]))
    if diffs:
        print('  cost: mean %+.1f s, range %+.1f..%+.1f s over %d runs; largest span after %.1f s' % (
            sum(diffs) / len(diffs), min(diffs), max(diffs), len(diffs), max(v['after'] for v in runs.values() if 'after' in v)))
