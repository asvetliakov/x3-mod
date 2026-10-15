"""Round A: the compact per-run figures of named casts from a fixture stdout (every seed's row needs X3M_C_ALL=1): launched, span, the
merges (count, done, max, late), stops (count, max, long), slow / lateral frames, episodes, uncertified frames, the closest capsule and
box, nose at 7 km, P2's nose, the law stops, the queue hold. Usage: python3 -I rows.py <stdout> [cast ...] (no casts: every cast)."""
import re
import sys

out = sys.argv[1]
casts = sys.argv[2:]
LINE = re.compile(r'^scenario C (\S+) lane tf (\d+) \d+ fps seed (\d+): (.*)$')


def num(pat, s):
    m = re.search(pat, s)
    return m.group(1) if m else '?'


with open(out, encoding='utf-8', errors='replace') as f:
    for l in f:
        m = LINE.match(l)
        if not m or (casts and m.group(1) not in casts):
            continue
        r = m.group(4)
        print('%-15s tf%-2s seed %-8s launched %s span %s | merges %s stops %s slow %s lat %s | ep %s uncert %s capsule %s box %s | nose7 %s p2_nose %s '
              '| law stops %s | qhold %s | T_slot %s | refused %s' % (
                  m.group(1), m.group(2), m.group(3), num(r'launched (\d+/\d+)', r), num(r'span (-?[\d.]+) s', r),
                  num(r'merges (\d+ done \d+ max [\d.]+ s late \d+)', r), num(r'stops (\d+ max [\d.]+ s long \d+)', r), num(r'slow (\d+)', r),
                  num(r'lat (\d+)', r), num(r'episodes (\d+)', r), num(r'uncert (\d+)', r), num(r'capsule (-?\d+)', r), num(r'box (-?\d+)', r),
                  num(r'nose7 ([\d.]+ deg \(\d+\))', r), num(r'p2_nose ([\d.]+)', r), num(r'law stops (\d+ max [\d.]+ s long \d+)', r),
                  num(r'qhold (\d+ max [\d.]+ s over -?\d+ rel -?\d+)', r), num(r'T_slot ([\d.]+\.\.[\d.]+)', r), num(r'refused (\d+)', r)))
