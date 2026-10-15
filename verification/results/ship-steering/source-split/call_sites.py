"""The direct calls from the ship steering hook entries and their callees in a DLL: for every function reachable from the entries by
direct calls (within the module's own functions: x3m_ship_*, the anonymous / x3m_ship_steering_rt namespaces, the cores'
x3m::ship_avoidance and x3m::ship_steering namespaces), the callee and the count of call instructions. Comparing two builds shows
which calls the source split turned from inlined into out-of-line calls.
  python3 -I call_sites.py BEFORE.dll AFTER.dll [--objdump i686-w64-mingw32-objdump]"""
import argparse, re, subprocess, sys
from collections import Counter

ENTRIES = ('x3m_ship_steering_enter', 'x3m_ship_steering_clamp', 'x3m_ship_avoid_suppress', 'x3m_ship_gate_stop_a', 'x3m_ship_gate_stop_b',
           'x3m_ship_gate_launch', 'x3m_ship_gate_arrival')
OURS = re.compile(r'^(x3m_ship_|\(anonymous namespace\)::|x3m_ship_steering_rt::|x3m::ship_avoidance::|x3m::ship_steering::|'
                  r'[\w:<>]+ x3m::ship_avoidance::)')


def norm(n):
    n = n.replace('x3m_ship_steering_rt::', 'anon::').replace('(anonymous namespace)::', 'anon::').replace('anon::anon::', 'anon::')
    n = re.sub(r' \[clone \.(part|isra|constprop|cold)\.\d+\]', '', n)
    return re.sub(r'\(.*', '', n)  # the name without its parameter list


def graph(dll, objdump):
    out = subprocess.run([objdump, '-d', '--no-show-raw-insn', '-C', '-w', dll], capture_output=True, text=True, check=True).stdout
    calls = {}
    cur = None
    for line in out.split('\n'):
        m = re.match(r'^[0-9a-f]+ <(.*)>:$', line)
        if m:
            cur = m.group(1)
            calls.setdefault(cur, Counter())
            continue
        m = re.search(r'\bcall\s+[0-9a-f]+ <([^>]*)>', line)
        if m and cur:
            calls[cur][re.sub(r'\+0x[0-9a-f]+$', '', m.group(1))] += 1
    return calls


def reach(calls):
    seen = {}
    stack = [n for n in calls if n in ENTRIES]
    while stack:
        n = stack.pop()
        key = norm(n)
        if n in seen:
            continue
        seen[n] = calls.get(n, Counter())
        for t in seen[n]:
            if OURS.match(t) and t in calls and t not in seen:
                stack.append(t)
    edges = Counter()
    for n, c in seen.items():
        for t, k in c.items():
            if OURS.match(t):
                edges[(norm(n), norm(t))] += k
    return edges


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('before')
    ap.add_argument('after')
    ap.add_argument('--objdump', default='i686-w64-mingw32-objdump')
    a = ap.parse_args()
    eb = reach(graph(a.before, a.objdump))
    ea = reach(graph(a.after, a.objdump))
    print('call edges from the hook entries: before %d (%d calls), after %d (%d calls)' % (len(eb), sum(eb.values()), len(ea), sum(ea.values())))
    for tag, x, y in (('only after', ea, eb), ('only before', eb, ea)):
        d = sorted(k for k in x if k not in y)
        print('%s: %d' % (tag, len(d)))
        for k in d:
            print('    %s -> %s (%d)' % (k[0][:70], k[1][:70], x[k]))
    ch = sorted(k for k in ea if k in eb and ea[k] != eb[k])
    print('count changed: %d' % len(ch))
    for k in ch:
        print('    %s -> %s (%d -> %d)' % (k[0][:70], k[1][:70], eb[k], ea[k]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
