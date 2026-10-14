"""Key-off identity: the fixture's stdout before (baseline binary) and after the lane change, with the host timing fields removed
(cost benches, *_us* fields, 'N us/ns/ms' figures): python3 keyoff_diff.py <before.txt> <after.txt>. Prints the differing line count.
The after binary runs with X3M_LANE_OFF=1 (the full and --capital runs end with the lane's scenario C since lane stage 1; the variable
skips it, so the comparison stays line for line, the check counts included)."""
import re, sys
def norm(path):
    out = []
    for line in open(path):
        if re.search(r'cost|bench|ms per|us per|ns per', line):
            continue
        line = re.sub(r'[A-Za-z_0-9]*us[A-Za-z_0-9 /]*=[0-9.e+/-]+', '', line)
        line = re.sub(r'[0-9.]+ (us|ns)\b', '', line)
        out.append(line)
    return out
a, b = norm(sys.argv[1]), norm(sys.argv[2])
import difflib
d = [l for l in difflib.unified_diff(a, b, lineterm='', n=0) if l.startswith(('+', '-')) and not l.startswith(('+++', '---'))]
print('lines before %d after %d differing %d' % (len(a), len(b), len(d)))
for l in d[:10]: print(l[:240].rstrip())
