"""Lane stage 1, scenario C (convoy-gate-transit.md section 6.11): builds the host fixture (verification/probe/ship_steering_host.cpp)
at the design constants and at one changed value of each (the X3M_LANE_* build macros of ship_gate_ctl_core.h), runs `--lane` with
every seed printed, and summarises per build: the failed checks, per variant the 15 km -> last launch span (min..max over seeds and
SETA steps 1, 6, 10), and the figures each constant acts on (merge duration, brief stops, the closest capsule gap, member 0x12
episodes, T_slot, launch gaps). No Wine, no game. Usage: python3 scenario_c.py [--quick] > scenario_c.txt (--quick: the design build
only)."""
import os, re, subprocess, sys, tempfile
from collections import defaultdict

ROOT = os.path.dirname(os.path.abspath(__file__))
for _ in range(4):
    ROOT = os.path.dirname(ROOT)
SRC = os.path.join(ROOT, 'verification', 'probe', 'ship_steering_host.cpp')
BUILDS = [  # (name, -D macros): the design values (g_req 500 m and tau_m 4 s since the lane stage 1 measurement; the design note said 400 m
    ('design', []),  # and 8 s; the escort's earlier join end, X3M_LANE_ESC_JOIN_K x lambda_max at the lane speed, is new in stage 1)
    ('g_req=400', ['-DX3M_LANE_GAP_M=400.0f']),
    ('tau_m=8', ['-DX3M_LANE_TAU_MERGE_S=8.0f']),
    ('tau_f=5', ['-DX3M_LANE_TAU_FOLLOW_S=5.0f']),
    ('T_stop=10s', ['-DX3M_LANE_STOP_MS=10000u']),
    ('lambda_max=10s', ['-DX3M_LANE_LAMBDA_MAX_MS=10000u']),  # review item 1: the hold horizon of HEAD's value (the reserve is the gate)
    ('crawl=0', ['-DX3M_LANE_CRAWL_M=0.0f']),                  # review item 1: the reserve stops instead of crawling
    ('drift=100', ['-DX3M_LANE_DRIFT_M=100.0f']),              # review items 5/7: the order-0 hold's lateral drift bound
    ('d_end=1.8km', ['-DX3M_LANE_END_M=1800.0f']),
    ('esc_join=0', ['-DX3M_LANE_ESC_JOIN_K=0.0f']),
    ('esc_join=0.5', ['-DX3M_LANE_ESC_JOIN_K=0.5f']),
]
LINE = re.compile(r'^scenario C (\S+) lane tf (\d+) \d+ fps seed (\d+): (.*)$')


def num(pat, s, default=float('nan')):
    m = re.search(pat, s)
    return float(m.group(1)) if m else default


def run(name, flags, d):
    exe = os.path.join(d, 'fx_' + re.sub(r'\W', '_', name))
    b = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror'] + flags + [SRC, '-o', exe], capture_output=True, text=True)
    if b.returncode:
        return None, b.stderr[-2000:]
    r = subprocess.run([exe, '--lane'], capture_output=True, text=True, env=dict(os.environ, X3M_C_ALL='1'))
    return r.stdout, r.stderr


def summarise(name, out, err):
    fails = re.search(r'failures=(\d+)', out)
    print('== build %s: checks %s failures %s; control: %s' % (
        name, re.search(r'checks=(\d+)', out).group(1), fails.group(1) if fails else '?',
        (re.search(r'control \(key off, the pre-lane core\): (.*)', out) or re.search('$', '')).group(0).split(': ', 1)[-1]))
    per = defaultdict(list)
    for line in out.splitlines():
        m = LINE.match(line)
        if m:
            per[m.group(1)].append((int(m.group(2)), line))
    for tag, rows in per.items():
        spans = defaultdict(list)
        mmax = stops = stopmax = episodes = uncert = 0.0
        caps, tslot, gaps = [], [], []
        for tf, l in rows:
            spans[tf].append(num(r'span (-?[\d.]+) s', l))
            mmax = max(mmax, num(r'merges \d+ done \d+ max ([\d.]+) s', l, 0))
            stops += num(r'stops (\d+)', l, 0)
            stopmax = max(stopmax, num(r'stops \d+ max ([\d.]+) s', l, 0))
            episodes += num(r'episodes (\d+)', l, 0)
            uncert += num(r'uncert (\d+)', l, 0)
            caps.append(num(r'capsule (-?[\d.]+)', l))
            tslot.append((tf, num(r'T_slot ([\d.]+)\.\.', l), num(r'T_slot [\d.]+\.\.([\d.]+)', l)))
            gaps.append(num(r'launch gaps -?[\d.]+\.\.([\d.]+)', l))
        sp = ' '.join('tf%d %.1f..%.1f' % (tf, min(v), max(v)) for tf, v in sorted(spans.items()))
        ts = ' '.join('tf%d %.1f..%.1f' % (tf, min(a for t, a, _ in tslot if t == tf), max(b for t, _, b in tslot if t == tf))
                      for tf in sorted(spans))
        print('  %-15s span %s | T_slot %s | merge_max %.1f s stops %d (max %.1f s) capsule_min %.0f m episodes %d uncert %d gap_max %.1f s'
              % (tag, sp, ts, mmax, stops, stopmax, min(caps), episodes, uncert, max(gaps)))
    for l in out.splitlines():
        if l.startswith('lane cost'):
            print('  ' + l)
    failed = defaultdict(int)
    for l in err.splitlines():
        if l.startswith('FAIL'):
            failed[re.sub(r' seed \d+', '', l[5:])] += 1
    for k in sorted(failed):
        print('  FAIL x%d %s' % (failed[k], k))


def main():
    quick = '--quick' in sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix='x3-lane-c-') as d:
        for name, flags in (BUILDS[:1] if quick else BUILDS):
            out, err = run(name, flags, d)
            if out is None:
                print('== build %s: BUILD FAILED\n%s' % (name, err))
                continue
            summarise(name, out, err)


if __name__ == '__main__':
    main()
