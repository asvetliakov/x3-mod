"""B.9 review item 2 (b9_head_gap_diag.txt): why the pre-review B.9 controller (commit 511ffbf5) left 3 of 36 extra-seed column runs bad
(60 fps seed 109, 144 fps seeds 104 and 106). Extracts that commit's proxy headers and fixture into a scratch dir, adds a debug print to
try_yield (the yielder's order, whether it is in its order-0 gap, its plan speed and cap) and a one-run switch to the fixture
(X3M_B9_RUN=<fps>,<seed> runs only that run), runs each bad run with X3M_B9_UNCERT and prints the first uncertified frames and the
yielder's decisions from 0.5 s before the flip to 1.5 s after it. The DLL, Wine and the game are not involved.
Usage: python3 -I b9_head_gap_diag.py <worktree> <scratch dir> [rev, default 511ffbf5] > b9_head_gap_diag.txt"""
import os
import re
import shutil
import subprocess
import sys

wt, out = sys.argv[1], sys.argv[2]
rev = sys.argv[3] if len(sys.argv) > 3 else '511ffbf5'
d = os.path.join(out, 'headdiag')
shutil.rmtree(d, ignore_errors=True)
os.makedirs(os.path.join(d, 'src', 'proxy'))
os.makedirs(os.path.join(d, 'verification', 'probe'))


def show(path):
    return subprocess.check_output(['git', '-C', wt, 'show', '%s:%s' % (rev, path)], text=True)


for n in subprocess.check_output(['git', '-C', wt, 'ls-tree', '--name-only', rev, 'src/proxy/'], text=True).split():
    if n.endswith('.h'):
        open(os.path.join(d, n), 'w').write(show(n))
fx = show('verification/probe/ship_steering_host.cpp')
a = '''    for (int fps : kFps)
        for (std::uint32_t seed : kSeeds) {
            b9_table_run = true;
            const TResult r = run_traffic(su, fps, seed);'''
b = '''    const bool one = std::getenv("X3M_B9_RUN") != nullptr;
    for (int fps : kFps)
        for (std::uint32_t seed0 : kSeeds) {
            if (one && (fps != b9_table_fps || seed0 != kSeeds[0])) continue;
            const std::uint32_t seed = one ? b9_table_seed : seed0;
            b9_table_run = true;
            const TResult r = run_traffic(su, fps, seed);'''
assert fx.count(a) == 1
open(os.path.join(d, 'verification', 'probe', 'ship_steering_host.cpp'), 'w').write(fx.replace(a, b))
core = os.path.join(d, 'src', 'proxy', 'ship_gate_ctl_core.h')
s = open(core).read()
a = '''    if (!can_yield(Y)) return false;
    const float v = plan_speed(g, Y) > 0.0f ? plan_speed(g, Y) : Y.st.v;'''
b = '''    if (!can_yield(Y)) { printf("DBGY t=%u ship=%x cannot_yield order=0x%x in_gap=%d\\n", now, Y.ship, Y.st.order, int(in_gap(Y))); return false; }
    const float v = plan_speed(g, Y) > 0.0f ? plan_speed(g, Y) : Y.st.v;
    printf("DBGY t=%u ship=%x ladder_from=%.1f order=0x%x v_plan=%.1f cap=%.1f\\n", now, Y.ship, v, Y.st.order, Y.v_plan, Y.cap);'''
assert s.count(a) == 1
s = s.replace(a, b).replace('#include <cstdint>', '#include <cstdint>\n#include <cstdio>', 1)
open(core, 'w').write(s)
exe = os.path.join(d, 'host')
subprocess.check_call(['c++', '-std=c++17', '-O2', os.path.join(d, 'verification', 'probe', 'ship_steering_host.cpp'), '-o', exe])
print('# B.9 review item 2: the pre-review controller (%s) on its 3 bad extra-seed column runs; DBGY rows: the yielder\'s try_yield' % rev)
for run in ('60,109', '144,104', '144,106'):
    env = dict(os.environ, X3M_B9_RUN=run, X3M_B9_UNCERT='1')
    env.pop('X3M_SEQ_OFF', None)
    lines = subprocess.run([exe, '--b9'], env=env, capture_output=True, text=True).stdout.splitlines()
    print('\n== %s fps,seed ==' % run)
    for l in lines:
        if l.startswith('sb B.9 column ') and 'table' not in l:
            print(l)
    unc = [l for l in lines if 'b9uncert' in l]
    for l in unc[:2]:
        print(l)
    if not unc:
        continue
    t0 = float(re.search(r't=([\d.]+)', unc[0]).group(1))
    t_last = float(re.search(r't=([\d.]+)', unc[-1]).group(1))
    print('  uncertified frames %d from %.3f to %.3f s' % (len(unc), t0, t_last))
    for l in lines:
        if l.startswith('DBGY'):
            t = int(re.search(r't=(\d+)', l).group(1)) / 1000.0
            if t0 - 0.5 <= t <= t0 + 1.5:
                print(l)
