"""B.5 fixture identity: compare two ship_steering_host stdouts line by line with the host timings masked (fixture_lines_b.py's masks),
scenario T and the stage-B lines included: B.5 adds the live set's proofs and leaves to scenario T. The new
'b5 ' lines and the count line are reported separately; the host-timing targets' met/MISSED is masked too. --t-may-differ: pass when only scenario T lines differ (B.5's leaves change T;
the proofs alone, X3M_B5_NO_LEAVE=1, must not). Usage: fixture_lines_b5.py [--t-may-differ] <before.txt> <after.txt>"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fixture_lines_b import load, norm as norm_b  # noqa: E402

MET = re.compile(r'(target [^:]*: )(met|MISSED)\b')  # a host-timing target's verdict (Fable review 5)


def norm(line):
    return MET.sub(lambda m: m.group(1) + 'T', norm_b(line))


def main():
    args = [x for x in sys.argv[1:] if x != '--t-may-differ']
    t_may = len(args) != len(sys.argv) - 1
    a, b = load(args[0]), load(args[1])
    count = lambda ls: [l for l in ls if l.startswith('ship_steering_host checks=')]
    keep = lambda ls: [norm(l) for l in ls if not l.startswith('ship_steering_host checks=') and not l.startswith('b5 ')]
    a_body, b_body = keep(a), keep(b)
    t_a = sum(1 for l in a_body if l.startswith('traffic T'))
    diff_idx = [i for i, (x, y) in enumerate(zip(a_body, b_body)) if x != y]
    diff = len(diff_idx) + abs(len(a_body) - len(b_body))
    t_diff = sum(1 for i in diff_idx if a_body[i].startswith('traffic T'))
    print(f'lines before {len(a)} after {len(b)}; compared {len(a_body)} vs {len(b_body)} (timings masked, scenario T {t_a} lines '
          f'included); differing {diff} (scenario T {t_diff}); new b5 lines {sum(1 for l in b if l.startswith("b5 "))}')
    for i in diff_idx[:2]:
        print('difference:\n  - ' + a_body[i][:220] + '\n  + ' + b_body[i][:220])
    fields = collections.Counter()  # the differing key=value fields of the differing lines (one count per line and field)
    for i in diff_idx:
        x, y = re.split(r'\s+', a_body[i].strip()), re.split(r'\s+', b_body[i].strip())
        for k, (p, q) in enumerate(zip(x, y)):
            if p != q:
                fields[p.split('=')[0] if '=' in p else 'field%d' % k] += 1
    if fields:
        print('differing fields (lines): ' + ', '.join('%s %d' % kv for kv in fields.most_common()))
    print('count before: ' + ' | '.join(count(a)))
    print('count after:  ' + ' | '.join(count(b)))
    ok = diff == 0 or (t_may and diff == t_diff and len(a_body) == len(b_body))
    print('RESULT PASS' + (' (scenario T may differ)' if t_may else '') if ok else 'RESULT FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
