"""Lane-on identity across the stage B deletion (2026-10-15; docs/architecture/ship-steering-holistic-review.md rank 2): the host fixture's
stdout with the deleted core against the core before it, host timing fields excluded. Replaces keyoff_diff.py (retired: the key-off path
it guarded, stage B, is gone).

  python3 -I laneon_diff.py [--base 564b3df8] [--out DIR]     (no Wine, no game; about 5 minutes: the runs are sequential, so the
                                                               fixture's host-timing checks see no load from each other)

Builds (c++ -std=c++17 -O2 -Wall -Wextra -Werror, as the fixture's own header says):
  before  the working tree's fixture against the core headers of --base (git show <base>:src/proxy/<header> copies, ship_capital_lane_core.h
          included): every line the fixture prints is computed by the old core
  after   the working tree's fixture against the working tree's core
  base    --base's own fixture and core (the lane scenario only)
and diffs
  1. before vs after, --lane and the full run: every line, the lane casts (key on) and the key-off control casts alike; 0 expected (the core
     deletion changes no output the kept fixture exercises);
  2. base vs after, --lane: the lane-on lines (the rows of the key-off control runs, the parked cast's key-off baseline and the control
     summary and the checks totals dropped; the deleted 'petal N' field dropped from base's rows); 0 expected (the fixture's own edits to
     scenario C change no lane-on output).
Prints each comparison's line counts and differing lines; exit 1 on any difference or build failure.

Source split (2026-10-15, holistic review rank 3): the header set is every src/proxy/ship_*_core.h of the working tree or of --base (the
unit headers ship_gate_board/geometry/lane/certificate/arrival_core.h exist only after the split; the fixture includes the umbrella
ship_gate_ctl_core.h), and --capital is compared before vs after as well (1c).
  python3 -I laneon_diff.py --base bf12aaa2"""
import argparse, difflib, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../..'))
FIXTURE = 'verification/probe/ship_steering_host.cpp'
HEADERS = ('ship_steering_core.h', 'ship_avoidance_core.h', 'ship_avoidance_walk_core.h', 'ship_gate_ctl_core.h', 'ship_gate_ctl_live_core.h',
           'ship_capital_lane_core.h')


def headers(base):
    """HEADERS and every ship_*_core.h of the working tree or of base."""
    names = set(HEADERS) | {n for n in os.listdir(os.path.join(ROOT, 'src/proxy')) if re.match(r'ship_\w+_core\.h$', n)}
    r = subprocess.run(['git', '-C', ROOT, 'ls-tree', '--name-only', base, 'src/proxy/'], capture_output=True, text=True)
    names |= {os.path.basename(n) for n in r.stdout.split() if re.match(r'ship_\w+_core\.h$', os.path.basename(n))}
    return sorted(names)


def git_show(rev, path):
    r = subprocess.run(['git', '-C', ROOT, 'show', '%s:%s' % (rev, path)], capture_output=True)
    return r.stdout if r.returncode == 0 else None


def tree(dst, fixture_bytes, header_bytes):
    os.makedirs(os.path.join(dst, 'src/proxy'), exist_ok=True)
    os.makedirs(os.path.join(dst, 'verification/probe'), exist_ok=True)
    for name, data in header_bytes.items():
        if data is not None:
            open(os.path.join(dst, 'src/proxy', name), 'wb').write(data)
    open(os.path.join(dst, FIXTURE), 'wb').write(fixture_bytes)


def build(dst):
    exe = os.path.join(dst, 'host')
    r = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', os.path.join(dst, FIXTURE), '-o', exe], capture_output=True,
                       text=True)
    if r.returncode:
        sys.exit('build failed in %s:\n%s' % (dst, r.stderr[-3000:]))
    return exe


def run(exe, arg, out):
    with open(out, 'w') as f, open(out[:-4] + '.err', 'w') as e:
        subprocess.run([exe] + ([arg] if arg else []), stdout=f, stderr=e)
    return out


def norm(path, lane_on_only=False):
    out = []
    for line in open(path):
        if re.search(r'cost|bench|ms per|us per|ns per', line):  # host timings (keyoff_diff.py's rule)
            continue
        if lane_on_only and (re.search(r'key-off|key off', line) or line.startswith('ship_steering_host checks=')):
            continue  # (the totals: the key-off control's checks changed with stage B's deletion; both runs' totals are printed below)
        line = re.sub(r'[A-Za-z_0-9]*us[A-Za-z_0-9 /]*=[0-9.e+/-]+', '', line)
        line = re.sub(r'\bus [0-9.]+/[0-9.]+', '', line)  # (the lane profile build's host timing on the 'engine paths' row, 2026-10-16)
        line = re.sub(r'[0-9.]+ (us|ns)\b', '', line)
        if lane_on_only:
            line = re.sub(r'\| petal \d+ nose_away', '| nose_away', line)
        out.append(line)
    return out


def compare(tag, a, b):
    d = [l for l in difflib.unified_diff(a, b, lineterm='', n=0) if l.startswith(('+', '-')) and not l.startswith(('+++', '---'))]
    print('%s: lines before %d after %d differing %d' % (tag, len(a), len(b), len(d)))
    for l in d[:10]:
        print('   ', l[:240].rstrip())
    return len(d)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--base', default='564b3df8')
    ap.add_argument('--out', default=None, help='keep the trees and the stdouts here (default: a temporary directory)')
    a = ap.parse_args()
    work = a.out or tempfile.mkdtemp(prefix='laneon_diff_')
    fixture_now = open(os.path.join(ROOT, FIXTURE), 'rb').read()
    names = headers(a.base)
    headers_now = {h: (open(os.path.join(ROOT, 'src/proxy', h), 'rb').read() if os.path.exists(os.path.join(ROOT, 'src/proxy', h)) else None)
                   for h in names}
    headers_base = {h: git_show(a.base, 'src/proxy/' + h) for h in names}
    tree(os.path.join(work, 'before'), fixture_now, headers_base)
    tree(os.path.join(work, 'after'), fixture_now, headers_now)
    tree(os.path.join(work, 'base'), git_show(a.base, FIXTURE), headers_base)
    with ThreadPoolExecutor(3) as ex:
        exes = dict(zip(('before', 'after', 'base'), ex.map(build, [os.path.join(work, k) for k in ('before', 'after', 'base')])))
    jobs = [('before', ''), ('after', ''), ('before', '--lane'), ('after', '--lane'), ('base', '--lane'), ('before', '--capital'),
            ('after', '--capital')]
    outs = [run(exes[j[0]], j[1], os.path.join(work, '%s%s.txt' % (j[0], j[1].replace('-', '_')))) for j in jobs]
    o = dict(zip(['%s%s' % j for j in jobs], outs))
    bad = 0
    bad += compare('1 full run, before-core vs after', norm(o['before']), norm(o['after']))
    bad += compare('1 --lane, before-core vs after', norm(o['before--lane']), norm(o['after--lane']))
    bad += compare('1c --capital, before-core vs after', norm(o['before--capital']), norm(o['after--capital']))
    bad += compare('2 --lane lane-on rows, %s fixture+core vs after' % a.base, norm(o['base--lane'], True), norm(o['after--lane'], True))
    for k in o:
        tail = [l for l in open(o[k]) if l.startswith('ship_steering_host checks=')]
        fails = [l.strip() for l in open(o[k][:-4] + '.err') if l.startswith('FAIL')]
        print('%s: %s; FAIL rows %d%s' % (k, tail[-1].strip() if tail else 'no checks line', len(fails), (': ' + fails[0][:160]) if fails else ''))
    print('stdouts in %s' % work)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
