"""Cleanup 2026-10-16, item 3: identifiers defined in the ship steering sources (src/proxy/ship_*, own_ship*, sun_share_lane*) that no
other code in src/, verification/probe/ or tools/ names (comments stripped): DEFONLY; struct fields whose every other mention is an
assignment: WRITEONLY? (a heuristic: x++ reads too). usage: python3 -I deadscan.py ROOT > deadscan.txt (seconds; no build)"""
import os, re, sys
from collections import Counter, defaultdict
root = sys.argv[1]
srcs = [os.path.join(root, 'src/proxy', n) for n in sorted(os.listdir(os.path.join(root, 'src/proxy')))
        if re.match(r'(ship_|own_ship|sun_share_lane)', n)]
scan = []
for d in ('src', 'verification/probe', 'tools'):
    for dp, dn, fn in os.walk(os.path.join(root, d)):
        for f in fn:
            if f.endswith(('.h', '.cpp', '.py', '.c', '.hpp')):
                scan.append(os.path.join(dp, f))
text = {p: open(p, errors='replace').read() for p in scan}
# strip comments for counting
def strip(s, py):
    if py:
        return re.sub(r'#.*', '', s)
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    return re.sub(r'//.*', '', s)
code = {p: strip(t, p.endswith('.py')) for p, t in text.items()}
words = Counter()
assign = Counter()
for p, c in code.items():
    for w in re.findall(r'\b[A-Za-z_]\w*\b', c):
        words[w] += 1
    for w in re.findall(r'\.([A-Za-z_]\w*)\s*(?:=(?!=)|\+\+|--|\+=|-=|\|=)', c):
        assign[w] += 1
    for w in re.findall(r'(?:\+\+|--)\s*\w+(?:\[[^]]*\])?\.([A-Za-z_]\w*)', c):
        assign[w] += 1
defs = defaultdict(set)
for p in srcs:
    c = code[p]
    for m in re.finditer(r'(?:inline|static)\s+[\w:<>,\s\*&]+?\b([a-z_]\w*)\s*\(', c):
        defs[m.group(1)].add(('fn', os.path.basename(p)))
    for m in re.finditer(r'constexpr\s+[\w:]+\s+(k\w+)', c):
        defs[m.group(1)].add(('const', os.path.basename(p)))
    for m in re.finditer(r'^\s+(?:std::)?(?:u?int\d+_t|float|double|bool|unsigned|int|Counter|Sb|Role|Why|JoinParams)\s+([^;(]+);', c, flags=re.M):
        for part in m.group(1).split(','):
            n = re.match(r'\s*\*?\s*([A-Za-z_]\w*)', part)
            if n:
                defs[n.group(1)].add(('field', os.path.basename(p)))
for name in sorted(defs):
    n = words[name]
    kinds = ','.join(sorted({k for k, _ in defs[name]}))
    files = ','.join(sorted({f for _, f in defs[name]}))
    if n <= 1:
        print('DEFONLY %-32s %s %s' % (name, kinds, files))
    elif 'field' in kinds and assign[name] >= n - 1:
        print('WRITEONLY? %-32s n=%d assigns=%d %s' % (name, n, assign[name], files))
