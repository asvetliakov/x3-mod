"""Compare two ship_steering_host stdouts line by line with the host timings masked (the traffic controller's stage-B key-off identity on
the fixture: every line outside scenario T and the stage-B unit and cost lines equal apart from timings; scenario T runs with the key on and
is stage B's change). Lines starting 'traffic T', 'TARGET MISS traffic T', 'FAIL traffic T', 'sb ', 'FAIL sb' and the count line are
reported separately. Usage: fixture_lines_b.py <before.txt> <after.txt>"""
import re
import sys

TIMING = [re.compile(p) for p in (r'-?\d+(?:\.\d+)?\s?(?:us|ns|ms per|µs)\b', r'(_us[a-z_/]*=)[-\d./]+', r'(us_max=)[-\d.]+',
                                  r'(eval_us_max=)[-\d.]+', r'(mean/max=)[-\d./]+',
                                  r'(worst admitted frame )\d+ plain \+ \d+ in conflict')]
CHANGED = re.compile(r'^(?:traffic T|TARGET MISS traffic T|FAIL traffic T|sb |FAIL sb )')


def norm(line):
    for p in TIMING:
        line = p.sub(lambda m: (m.group(1) if m.groups() else '') + 'T', line)
    return line


def load(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return [l.rstrip('\n') for l in f]


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    count = lambda ls: [l for l in ls if l.startswith('ship_steering_host checks=')]
    keep = lambda ls: [norm(l) for l in ls if not l.startswith('ship_steering_host checks=') and not CHANGED.match(l)]
    a_body, b_body = keep(a), keep(b)
    n_a, n_b = sum(1 for l in a if CHANGED.match(l)), sum(1 for l in b if CHANGED.match(l))
    diff = sum(1 for x, y in zip(a_body, b_body) if x != y) + abs(len(a_body) - len(b_body))
    first = next((i for i, (x, y) in enumerate(zip(a_body, b_body)) if x != y), None)
    print(f'lines before {len(a)} after {len(b)}; compared {len(a_body)} vs {len(b_body)} (timings masked); differing {diff}; '
          f'scenario T / stage-B lines before {n_a} after {n_b}')
    if first is not None:
        print('first difference:\n  - ' + a_body[first][:200] + '\n  + ' + b_body[first][:200])
    print('count before: ' + ' | '.join(count(a)))
    print('count after:  ' + ' | '.join(count(b)))
    print('RESULT PASS' if diff == 0 else 'RESULT FAIL')
    return 0 if diff == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
