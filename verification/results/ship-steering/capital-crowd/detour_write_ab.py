"""Amendment 16 detour-write fix, before/after: the host fixture's stdout before the fix and after it, line by line with the
timing fields (host microseconds) removed; prints the lines that differ, grouped by their leading words, and the scenario-M
simultaneous TARGET MISS counts per target.
Usage: python3 -I verification/results/ship-steering/capital-crowd/detour_write_ab.py \
           verification/results/ship-steering/capital-crowd/ship_steering_host_before_detour_write.txt \
           verification/results/ship-steering/ship_steering_host.txt > verification/results/ship-steering/capital-crowd/detour_write_ab.txt"""
import re
import sys

TIMING = re.compile(r'(lane_us_max|E_us mean/max|eval_us_max|slowest evaluation|us per capital decision|us \(host|cost)[^ ]*( [^ ]*)?')


def norm(line):
    return TIMING.sub('<t>', line.rstrip('\n'))


def misses(lines):
    out = {}
    for line in lines:
        if line.startswith('TARGET MISS'):
            key = line.split(': ', 1)[1].split(' (')[0]
            out[key] = out.get(key, 0) + 1
    return out


def main():
    before = open(sys.argv[1]).readlines()
    after = open(sys.argv[2]).readlines()
    print(f'lines before={len(before)} after={len(after)}')
    b = [norm(x) for x in before if 'TARGET MISS' not in x]
    a = [norm(x) for x in after if 'TARGET MISS' not in x]
    groups = {}
    for x, y in zip(b, a):
        if x != y:
            key = ' '.join(y.split()[:4])
            groups.setdefault(key, []).append((x, y))
    print(f'differing lines (timing removed): {sum(len(v) for v in groups.values())}')
    for key, pairs in sorted(groups.items()):
        print(f'  {len(pairs):3d}  {key}')
    for name, lines in (('before', before), ('after', after)):
        m = misses(lines)
        print(f'TARGET MISS {name}: {sum(m.values())}', {k: v for k, v in sorted(m.items())})



def extremes(path):
    """The simultaneous runs' worst values from the TARGET MISS rows: fallback frames (and seconds at the run's fps), the
    longest rotate after a slow join, the minimum box separation outside the grid zone."""
    fb, rot, sep = (0, 0, 0), 0.0, 1e18
    for line in open(path):
        if not line.startswith('TARGET MISS M simultaneous'):
            continue
        fps = int(re.search(r' (\d+) fps', line).group(1))
        m = re.search(r'no aim-at-A fallback \(crossings (\d+), frames (\d+)\)', line)
        if m and int(m.group(2)) / fps > (fb[0] / fb[1] if fb[1] else 0):
            fb = (int(m.group(2)), fps, int(m.group(1)))
        m = re.search(r'rotate <= 5 s \(max ([\d.]+) s', line)
        if m:
            rot = max(rot, float(m.group(1)))
        m = re.search(r'min separation (-?\d+) m', line)
        if m:
            sep = min(sep, float(m.group(1)))
    return fb, rot, sep


def summary():
    for name, p in (('before', sys.argv[1]), ('after', sys.argv[2])):
        (frames, fps, crossings), rot, sep = extremes(p)
        print(f'simultaneous {name}: most fallback {frames} frames at {fps} fps = {frames / fps:.1f} s (summed over ships, {crossings} '
              f'fallback crossings), longest rotate {rot:.1f} s, minimum box separation {sep:.0f} m')


if __name__ == '__main__':
    main()
    summary()
