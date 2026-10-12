"""Amendment 16b with the review fixes, before/after: two host-fixture stdouts compared with the timing fields (host
microseconds) removed, aligned by difflib (lines were added), the changed / removed / added lines counted by their leading
words, and the TARGET MISS rows counted per target. Usage:
    python3 -I verification/results/ship-steering/capital-crowd/seq16b_ab.py BEFORE.txt AFTER.txt [--show]"""
import difflib
import re
import sys

TIMING = re.compile(r'(lane_us_max|E_us mean/max|eval_us_max|slowest evaluation|us per capital decision|us \(host|cost|ns per)[^ ]*( [^ ]*)?')


MEANMAX = re.compile(r'mean/max=[0-9./]+')


def norm(line):
    return MEANMAX.sub('<t>', TIMING.sub('<t>', line.rstrip('\n')))


def key(line):
    return ' '.join(line.split()[:4])


def misses(lines):
    out = {}
    for line in lines:
        if line.startswith('TARGET MISS'):
            k = re.sub(r' \((?!c\))[^)]*\)', '', line.split(': ', 1)[1].rstrip('\n'))  # parentheticals dropped, "(c)" kept
            k = re.sub(r'-?\d+(\.\d+)?', '#', k)
            out[k] = out.get(k, 0) + 1
    return out


def main():
    before = [norm(x) for x in open(sys.argv[1])]
    after = [norm(x) for x in open(sys.argv[2])]
    show = '--show' in sys.argv
    print(f'lines before={len(before)} after={len(after)}')
    groups = {}
    for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, before, after, autojunk=False).get_opcodes():
        if op == 'equal':
            continue
        for x in before[i1:i2]:
            groups.setdefault(('-', key(x)), []).append(x)
        for y in after[j1:j2]:
            groups.setdefault(('+', key(y)), []).append(y)
    print(f'differing lines (timing removed): -{sum(len(v) for (s, _), v in groups.items() if s == "-")} '
          f'+{sum(len(v) for (s, _), v in groups.items() if s == "+")}')
    for (sign, k), v in sorted(groups.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        print(f'  {sign}{len(v):4d}  {k}')
        if show:
            for x in v[:3]:
                print(f'          {x[:240]}')
    for name, lines in (('before', before), ('after', after)):
        m = misses(lines)
        print(f'{name}: TARGET MISS rows {sum(m.values())}')
        for k, n in sorted(m.items()):
            print(f'  {n:3d}  {k}')


if __name__ == '__main__':
    main()
