"""Site W's removal (stage B deletion, 2026-10-15): the site verifier's --json report before (main 564b3df8) and after, with every
entry that names site W dropped (the ship_gate_warp_in site, the w_* gate facts, resume 'w', w_flags_dead_until_test, RET_POP's W),
compared field for field. Usage: python3 -I sites_w_diff.py <before.json> <after.json> (both from
verify_ship_steering_sites.py --exe /mnt/c/X3/X3AP.exe --json). Prints the check counts and every differing path (0 expected)."""
import json, sys


def is_w(key):
    k = str(key)
    return k == 'w' or k.startswith('w_') or 'warp_in' in k or k == 'w_flags_dead_until_test'


def strip(o):
    if isinstance(o, dict):
        return {k: strip(v) for k, v in o.items() if not is_w(k) and not (isinstance(v, dict) and is_w(v.get('name', '')))}
    if isinstance(o, list):
        return [strip(v) for v in o if not (isinstance(v, dict) and is_w(v.get('name', '')))]
    if isinstance(o, str):  # the pinned prose of gate_sites names W
        return o.split("; W: ")[0].rstrip("'")
    return o


def diff(a, b, path, out):
    if type(a) != type(b):
        out.append('%s: type %s -> %s' % (path, type(a).__name__, type(b).__name__))
    elif isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a or k not in b:
                out.append('%s.%s: only %s' % (path, k, 'after' if k not in a else 'before'))
            else:
                diff(a[k], b[k], '%s.%s' % (path, k), out)
    elif isinstance(a, list):
        if len(a) != len(b):
            out.append('%s: length %d -> %d' % (path, len(a), len(b)))
        for i, (x, y) in enumerate(zip(a, b)):
            diff(x, y, '%s[%d]' % (path, i), out)
    elif a != b:
        out.append('%s: %r -> %r' % (path, a, b))


before, after = json.load(open(sys.argv[1])), json.load(open(sys.argv[2]))
print('before %s %d checks, after %s %d checks' % (before['result'], len(before['checks']), after['result'], len(after['checks'])))
out = []
diff(strip(before), strip(after), '', out)
print('differing paths without site W: %d' % len(out))
for line in out[:20]:
    print(' ', line[:200])
