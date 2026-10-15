import re, sys
# Round C: the --lane stdout of d2871285 (its fixture and core) against the working tree's, the scenario C rows of every cast round C did
# not un-park (x12_near / x12_far excluded), host timing fields dropped; the new engine-path row fields (" | loop ...") cut. Prints the
# casts with identical rows and those that differ (with the first differing pair).
# usage: python3 -I laneon_cmp.py <base --lane stdout> <tree --lane stdout>
TIMING = re.compile(r'(\d+(\.\d+)? us|ctl_frame_us\S*|us [0-9.]+/[0-9.]+)')


def rows(path):
    out = {}
    for line in open(path):
        m = re.match(r'scenario C (\S+) ', line)
        if not m or m.group(1) in ('x12_near', 'x12_far', 'control'):
            continue
        line = TIMING.sub('T', re.sub(r'us -?[0-9.]+/-?[0-9.]+', 'us T', line.rstrip('\n').split(' | loop ')[0]))
        out.setdefault(m.group(1), []).append(line)
    return out


a, b = rows(sys.argv[1]), rows(sys.argv[2])
same, diff = [], []
for cast in sorted(set(a) & set(b)):
    (same if a.get(cast) == b.get(cast) else diff).append(cast)
print('identical (%d): %s' % (len(same), ' '.join(same)))
print('in one run only (parked on one side): %s' % ' '.join(sorted(set(a) ^ set(b))))
print('differ (%d): %s' % (len(diff), ' '.join(diff)))
for cast in diff:
    for x, y in zip(a.get(cast, []), b.get(cast, [])):
        if x != y:
            print('  %s\n    base: %s\n    tree: %s' % (cast, x[:300], y[:300]))
            break
