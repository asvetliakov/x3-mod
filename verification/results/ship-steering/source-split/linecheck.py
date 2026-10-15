"""The source split of 2026-10-15 moves lines only: every line of the original file appears in the split files as often as before, and
the extra lines are the new files' prologues (#pragma once, the intro comments, the includes, the namespace openings and closings, the
declarations of the out-of-line functions another unit calls and the extern declarations of the stubs' slots) and the 'inline ' prefix
of the shared variables (checked against the original line it prefixes).
  git show bf12aaa2:src/proxy/ship_gate_ctl_core.h > /tmp/a.h
  python3 -I linecheck.py /tmp/a.h src/proxy ship_gate_board_core.h ... ship_gate_ctl_core.h
  git show bf12aaa2:src/proxy/ship_steering.cpp > /tmp/b.cpp
  python3 -I linecheck.py /tmp/b.cpp src/proxy ship_steering_internal.h ship_gate_ctl_glue.h ... ship_dock_glue.cpp
Prints the totals and every missing or extra line."""
import collections, re, sys
orig = open(sys.argv[1]).read().split('\n')
gen = []
for f in sys.argv[3:]:
    gen += open(sys.argv[2] + '/' + f).read().split('\n')


def norm(l):
    return l.rstrip()


skip = re.compile(r'^(namespace \{|namespace x3m_ship_steering_rt \{|namespace x3m::ship_steering \{|\} // namespace.*|\}|)$')
a = collections.Counter(norm(l) for l in orig if not skip.match(norm(l)))
b = collections.Counter()
for l in gen:
    l = norm(l)
    if skip.match(l):
        continue
    if l.startswith('inline ') and l[7:] in a and l not in a:
        l = l[7:]
    b[l] += 1
missing = a - b
extra = b - a
print('orig lines %d, generated %d; missing %d, extra %d' % (sum(a.values()), sum(b.values()), sum(missing.values()), sum(extra.values())))
for l, n in list(missing.items()):
    print('MISSING %d: %s' % (n, l[:150]))
for l, n in list(extra.items()):
    print('EXTRA %d: %s' % (n, l[:150]))
