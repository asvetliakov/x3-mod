"""B.5: how much scenario T moved with the leaves (launched members leave the board). For every numeric key=value field of the 'traffic T'
lines that differs between two fixture stdouts: lines differing, the sum before and after, and the largest absolute change.
Usage: scenario_t_b5.py <before.txt> <after.txt>"""
import re
import sys

NUM = re.compile(r'^-?\d+(?:\.\d+)?$')


def t_lines(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return [l.rstrip('\n') for l in f if l.startswith('traffic T')]


def kv(line):
    return {k: v for k, v in re.findall(r'(\S+?)=(\S+)', line)}


def main():
    a, b = t_lines(sys.argv[1]), t_lines(sys.argv[2])
    stats = {}
    for x, y in zip(a, b):
        if x == y:
            continue
        ka, kb = kv(x), kv(y)
        for k in ka:
            va, vb = ka[k].rstrip(','), kb.get(k, '').rstrip(',')
            if va == vb or not NUM.match(va) or not NUM.match(vb):
                continue
            s = stats.setdefault(k, [0, 0.0, 0.0, 0.0])
            s[0] += 1
            s[1] += float(va)
            s[2] += float(vb)
            s[3] = max(s[3], abs(float(vb) - float(va)))
    print(f'traffic T lines {len(a)} / {len(b)}, differing {sum(1 for x, y in zip(a, b) if x != y)}')
    for k, (n, sa, sb, mx) in sorted(stats.items(), key=lambda kv_: -kv_[1][0]):
        print(f'{k}: lines {n}, sum {sa:g} -> {sb:g}, largest change {mx:g}')


if __name__ == '__main__':
    main()
