"""The "before B.7" build of scenario_t_b7.txt: this commit's fixture and proxy headers, with one adaptation (asserted): lap_speed's B.7
escort branch is removed, so every escort laps toward B.6's release window again; gc::escort_stage_s stays (the fixture's window helper
and units call it; the controller no longer does). Scenario T, its B.6 / B.7 rows and assertions are the after build's source.
Usage: python3 -I scenario_t_b7_pre_build.py <worktree> <scratch dir>
then: c++ -std=c++17 -O2 -Wall -Wextra -Werror <scratch>/verification/probe/ship_steering_host.cpp -o pre && ./pre --traffic > pre.traffic
(the B.7 assertions fail there by design: the before figures are its "B.7:" rows)"""
import os
import shutil
import sys

wt, out = sys.argv[1], sys.argv[2]
os.makedirs(os.path.join(out, 'src', 'proxy'), exist_ok=True)
os.makedirs(os.path.join(out, 'verification', 'probe'), exist_ok=True)
for n in os.listdir(os.path.join(wt, 'src', 'proxy')):
    if n.endswith('.h'):
        shutil.copy(os.path.join(wt, 'src', 'proxy', n), os.path.join(out, 'src', 'proxy', n))
shutil.copy(os.path.join(wt, 'verification', 'probe', 'ship_steering_host.cpp'), os.path.join(out, 'verification', 'probe'))
p = os.path.join(out, 'src', 'proxy', 'ship_gate_ctl_core.h')
s = open(p).read()
b7 = '''    float open = kWinOpenS;
    if (m.role == Role::escort) { // B.7: an escort's window opens at its stage, inside the script's radius (no stage: W kept)
        const float st = escort_stage_s(pg);
        if (st >= 0.0f) open = st;
    }
    return lap_law(s, open, kWinCloseS, kLap, std::int32_t(t_rel - now), true, release_window(g, m));'''
assert s.count(b7) == 1
s = s.replace(b7, '    return lap_law(s, kWinOpenS, kWinCloseS, kLap, std::int32_t(t_rel - now), true, release_window(g, m));')
open(p, 'w').write(s)
