"""B.9 (Run168 column evictions): the record column_b9.txt. Builds the fixture against this tree's proxy headers ("after") and against
copies with B.9's controller changes reverted by asserted text substitution ("before" = all of them; the no* variants revert one each):
kin, the inbound rollout's in-plane share; law, the inbound law in the petal rollout; hold (review item 1), hold_law past the boundary and
for a holder (reverted: 60 m/s past the boundary, the plan speed in hold, the member's own ring flag); gapcap (review items 2-3), the order-0
gap's resume under the cap in force, the deferred yield in the gap and the kept cap; age (review item 2), plan_speed's signed v_plan age;
corner (second review), the cornering cap in the petal rollout; target (second review), the head's target for an inbound committed member.
Runs the column scenario (`--b9`: the review units, 3 frame rates x 3 seeds, its assertions and the 60 fps / seed 1 table) and the
extra-seed sweep (X3M_B9_SEEDS: seeds 100.. at each frame rate), and prints the summary rows, the per-frame table (1 s, from 60 s before the
rear's hold entry to 30 s after) and the rollouts' 10 s prediction error of both members (sample 20 against the flown position 10 s later,
before the rear's hold entry). The DLL, Wine and the game are not involved.
Usage: python3 -I column_b9.py <worktree> <scratch dir> [extra seeds, default 12] > column_b9.txt"""
import os
import re
import shutil
import statistics
import subprocess
import sys

# (old, new, optional): an optional substitution may find nothing (it edits another revert's text)
SUBS = {
    'kin': [('plant_step_to(m.params, vd, kStepS, k_lag, ta, tb, x, y, psi, upsi, w, v, k_in);',
             'plant_step_to(m.params, vd, kStepS, k_lag, ta, tb, x, y, psi, upsi, w, v); (void)k_in;', False)],
    'law': [('if (inb && vd > kPetalVmax && d2 < reach2) {', 'if (false && inb && vd > kPetalVmax && d2 < reach2) {', False),
            ('v_des < kPetalVmax ? v_des : kPetalVmax; (void)v_hold;', 'v_des; (void)v_hold;', True)],
    'hold': [('vd = inb ? top : v_hold < top ? v_hold : top;',
              'vd = inb || m.sb != Sb::inbound ? v_des : v_des < kPetalVmax ? v_des : kPetalVmax; (void)v_hold;', False),
             ('const bool entering = inb || !ringed;', 'const bool entering = m.sb == Sb::inbound || !m.ringed;', False)],
    'corner': [('if (v_corner < vd) vd = v_corner;', '(void)v_corner;', False)],
    'target': [('    return g.head_t_ms;\n}', '    return 0u * g.head_t_ms;\n}', False)],
    'gapcap': [('return c >= 0.0f && c < m.v_plan ? c : m.v_plan;', 'return c >= 0.0f ? m.v_plan : m.v_plan;', False),
               ('if (!can_yield(Y) && !gap_yield(Y)) return false;', 'if (!can_yield(Y)) return false;', False),
               ('if (in_gap(A) && cap_in_force(g, A, now) >= 0.0f)', 'if (false && in_gap(A) && cap_in_force(g, A, now) >= 0.0f)', False),
               ('else if (in_gap(m)) {', 'else if (false && in_gap(m)) {', False)],
    'age': [('std::int32_t(m.st.ms - m.v_plan_ms) <= std::int32_t(cap_age)) return m.v_plan;', 'm.st.ms - m.v_plan_ms <= cap_age) return m.v_plan;', False),
            ('std::int32_t(m.st.ms - m.v_plan_ms) <= std::int32_t(kGapMs + cap_age)', 'm.st.ms - m.v_plan_ms <= kGapMs + cap_age', False)],
}
ALL = ['hold', 'kin', 'law', 'gapcap', 'age', 'corner', 'target']  # hold before law (law's optional edit is on hold's text)
VARIANTS = {'after': [], 'before': ALL, 'nokin': ['kin'], 'nolaw': ['law'], 'nohold': ['hold'], 'nogapcap': ['gapcap'], 'noage': ['age'],
            'nocorner': ['corner'], 'notarget': ['target']}


def build(wt, out, name, reverts):
    d = os.path.join(out, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, 'src', 'proxy'))
    os.makedirs(os.path.join(d, 'verification', 'probe'))
    for n in os.listdir(os.path.join(wt, 'src', 'proxy')):
        if n.endswith('.h'):
            shutil.copy(os.path.join(wt, 'src', 'proxy', n), os.path.join(d, 'src', 'proxy', n))
    shutil.copy(os.path.join(wt, 'verification', 'probe', 'ship_steering_host.cpp'), os.path.join(d, 'verification', 'probe'))
    p = os.path.join(d, 'src', 'proxy', 'ship_gate_ctl_core.h')
    s = open(p).read()
    for r in reverts:
        for a, b, optional in SUBS[r]:
            assert s.count(a) == 1 or (optional and s.count(a) == 0), (name, a)
            s = s.replace(a, b)
    open(p, 'w').write(s)
    exe = os.path.join(d, 'host')
    subprocess.check_call(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', os.path.join(d, 'verification', 'probe', 'ship_steering_host.cpp'),
                           '-o', exe])
    return exe


def run(exe, extra_env):
    env = dict(os.environ)
    env.pop('X3M_SEQ_OFF', None)
    env.update(extra_env)
    return subprocess.run([exe, '--b9'], env=env, capture_output=True, text=True).stdout.splitlines()


def pred_error(lines, after_hold=False):
    rows = []
    for l in lines:
        if 'b9all' not in l:
            continue
        t = float(re.search(r' t=([\d.]+)', l).group(1))
        sb = re.search(r' sb=(\d+)/(\d+)', l)
        pa = [float(v) for v in re.search(r'pa=([-\d.,]+)', l).group(1).split(',')]
        pb = [float(v) for v in re.search(r'pb=([-\d.,]+)', l).group(1).split(',')]
        m = re.search(r'qa=([-\d.,]+)@([\d.]+) qb=([-\d.,]+)@([\d.]+)', l)
        rows.append((t, pa, pb, [float(v) for v in m.group(1).split(',')], float(m.group(2)), [float(v) for v in m.group(3).split(',')],
                     float(m.group(4)), int(sb.group(2))))
    hold = next((r[0] for r in rows if r[7] == 1), rows[-1][0] if rows else 0.0)

    def at(tt):
        best = min(rows, key=lambda r: abs(r[0] - tt))
        return best if abs(best[0] - tt) < 0.6 else None
    ea, eb = [], []
    for (t, pa, pb, qa, ra, qb, rb, sbr) in rows:
        if abs(t - round(t)) > 1e-6:
            continue
        if (not after_hold and t + 10.0 > hold) or (after_hold and (t < hold or t + 10.0 > hold + 30.0)):
            continue
        fa, fb = at(ra + 10.0), at(rb + 10.0)
        if not fa or not fb:
            continue
        ea.append(sum((qa[i] - fa[1][i]) ** 2 for i in range(3)) ** 0.5)
        eb.append(sum((qb[i] - fb[2][i]) ** 2 for i in range(3)) ** 0.5)
    return hold, ea, eb


def main():
    wt, out = sys.argv[1], sys.argv[2]
    seeds = sys.argv[3] if len(sys.argv) > 3 else '12'
    exes = {n: build(wt, out, n, r) for n, r in VARIANTS.items()}
    print('# B.9 column scenario (kTCol9; gate-traffic-controller.md B.9). before = the after tree with every B.9 change reverted (kin, law, '
          'hold, gapcap, age, corner, target: column_b9.py SUBS); the no* variants revert one each.')
    for name in ('before', 'after'):
        lines = run(exes[name], {'X3M_B9_ALL': '1'})
        print('\n== %s: the column runs (--b9) ==' % name)
        for l in lines:
            if (l.startswith('sb B.9 column ') and 'table' not in l or l.startswith('FAIL B.9') or l.startswith('FAIL sb B.9 review')
                    or l.startswith('sb B.9 review') or l.startswith('ship_steering_host checks')):
                print(l)
        table, k = False, 0
        print('-- the table, every second row (1 s; 60 fps, seed 1) --')
        for l in lines:
            if l.startswith('sb B.9 column table'):
                table = True
                print(l)
                continue
            if table and l.startswith('  t_rel'):
                print(l)
                continue
            if table and re.match(r'^\s+-?\d+\.\d\s', l):
                if k % 2 == 0:
                    print(l)
                k += 1
                continue
            if table and not l.startswith('  '):
                table = False
        caps = [float(re.search(r' t=([\d.]+)', l).group(1)) for l in lines
                if 'b9all' in l and float(re.search(r'capab=[-\d.]+/([-\d.]+)', l).group(1)) >= 0.0]
        print('-- the rear\'s first yield cap on the 500 ms rows (60 fps, seed 1): %s s; rows with a rear cap %d --' % (
            '%.1f' % caps[0] if caps else 'none', len(caps)))
        hold, ea, eb = pred_error(lines)
        print('-- the rollouts\' 10 s prediction error before the rear\'s hold entry (%.1f s): front median %.0f m p95 %.0f m (n %d), rear median '
              '%.0f m p95 %.0f m (n %d) --' % (hold, statistics.median(ea), sorted(ea)[int(0.95 * (len(ea) - 1))], len(ea), statistics.median(eb),
                                               sorted(eb)[int(0.95 * (len(eb) - 1))], len(eb)))
    print('\n== the extra-seed sweep (X3M_B9_SEEDS=%s: seeds 100.. at 30 / 60 / 144 fps; a run is bad on a 0x12 episode, an uncertified frame, no '
          'rear yield or a capsule contact) ==' % seeds)
    for name in ('after', 'before', 'nokin', 'nolaw', 'nohold', 'nogapcap', 'noage', 'nocorner', 'notarget'):
        lines = run(exes[name], {'X3M_B9_SEEDS': seeds})
        bad = [l for l in lines if l.startswith('b9seeds fps')]
        tot = [l for l in lines if l.startswith('b9seeds bad')]
        contact = sum(1 for l in bad if float(l.split('min_capsule ')[1]) <= 0.0)
        print('%-9s %s (with capsule contact %d)' % (name, tot[0] if tot else '?', contact))
    print('\n== the 10 s prediction error per variant (60 fps, seed 1; median / p95, m): before the rear\'s hold entry, and from it to 30 s '
          'after (the predictions made there whose 10 s fall inside) ==')

    def q(e):
        return '%.0f / %.0f' % (statistics.median(e), sorted(e)[int(0.95 * (len(e) - 1))]) if e else 'n/a'
    for name in ('after', 'before', 'nokin', 'nolaw', 'nohold', 'nogapcap', 'noage', 'nocorner', 'notarget'):
        lines = run(exes[name], {'X3M_B9_ALL': '1'})
        hold, ea, eb = pred_error(lines)
        _, fa, fb = pred_error(lines, True)
        print('%-9s rear hold entry %.1f s  before: front %s  rear %s  (n %d)  after: front %s  rear %s  (n %d)' % (
            name, hold, q(ea), q(eb), len(ea), q(fa), q(fb), len(fa)))


main()
