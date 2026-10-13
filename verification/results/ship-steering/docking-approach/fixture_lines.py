"""Compare two ship_steering_host stdouts line by line with the host timings masked (amendment 18's key-off identity on the
fixture's scenarios: every pre-existing line equal apart from timings). Lines of the 'scenario N' section (rows 'dock N', its
TARGET MISS and FAIL rows) and
the final count lines are reported separately. Usage: fixture_lines.py <before.txt> <after.txt>"""
import re
import sys

TIMING = [re.compile(p) for p in (r'-?\d+(?:\.\d+)?\s?(?:us|ns|ms per|µs)\b', r'(_us[a-z_/]*=)[-\d./]+', r'(us_max=)[-\d.]+',
                                  r'(eval_us_max=)[-\d.]+', r'(mean/max=)[-\d./]+',
                                  # the cost bench names the slowest admitted frame mix it measured (timing-dependent)
                                  r'(worst admitted frame )\d+ plain \+ \d+ in conflict')]
NEW = re.compile(r'^(?:dock N|TARGET MISS dock N|FAIL dock N)')


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
    a_body = [norm(l) for l in a if not l.startswith('ship_steering_host checks=') and not NEW.match(l)]
    b_body = [norm(l) for l in b if not l.startswith('ship_steering_host checks=') and not NEW.match(l)]
    n_new = sum(1 for l in b if NEW.match(l))
    diff = sum(1 for x, y in zip(a_body, b_body) if x != y) + abs(len(a_body) - len(b_body))
    first = next((i for i, (x, y) in enumerate(zip(a_body, b_body)) if x != y), None)
    print(f'lines before {len(a)} after {len(b)}; compared {len(a_body)} vs {len(b_body)} (timings masked); differing {diff}; '
          f'scenario N lines {n_new}')
    if first is not None:
        print('first difference:\n  - ' + a_body[first][:200] + '\n  + ' + b_body[first][:200])
    print('count before: ' + ' | '.join(count(a)))
    print('count after:  ' + ' | '.join(count(b)))
    print('RESULT PASS' if diff == 0 else 'RESULT FAIL')
    return 0 if diff == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
