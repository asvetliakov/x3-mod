"""The control builds of scenario_t_b8.txt: this commit's fixture and proxy headers with B.8's controller parts removed (each asserted).
mode b7 (the "before B.8" build): lap_speed no longer passes the join distance (d_join defaults to 0, which the B.8 unit shows is B.7's
law bit for bit), the escort target has no lead (kStageLeadMs + the step), and a late unringed holder keeps the 60 m/s cap (no join
profile): B.7's behaviour. mode nolead: only the lead is removed (the control for the lead's effect).
petal_join, escort_target and Member::t_rel_ms stay (the fixture's rows read them). Usage:
python3 -I scenario_t_b8_pre_build.py <worktree> <scratch dir> [b7|nolead]
then: c++ -std=c++17 -O2 -Wall -Wextra <scratch>/verification/probe/ship_steering_host.cpp -o pre && ./pre --traffic > pre.traffic
(B.8 assertions fail in mode b7 by design: the before figures are its "B.7:" and "B.8:" rows)"""
import os
import shutil
import sys

wt, out = sys.argv[1], sys.argv[2]
mode = sys.argv[3] if len(sys.argv) > 3 else 'b7'
os.makedirs(os.path.join(out, 'src', 'proxy'), exist_ok=True)
os.makedirs(os.path.join(out, 'verification', 'probe'), exist_ok=True)
for n in os.listdir(os.path.join(wt, 'src', 'proxy')):
    if n.endswith('.h'):
        shutil.copy(os.path.join(wt, 'src', 'proxy', n), os.path.join(out, 'src', 'proxy', n))
shutil.copy(os.path.join(wt, 'verification', 'probe', 'ship_steering_host.cpp'), os.path.join(out, 'verification', 'probe'))
p = os.path.join(out, 'src', 'proxy', 'ship_gate_ctl_core.h')
s = open(p).read()
edits = [('    t -= kStageLeadMs + g.step_ms;\n', '')]
if mode == 'b7':
    edits += [('true, release_window(g, m), d_join);', 'true, release_window(g, m));'),
              ('if (v >= kPetalVmax && t_rel && !m.ringed && m.petal >= 0) {', 'if (false) {')]
for a, b in edits:
    assert s.count(a) == 1, a
    s = s.replace(a, b)
open(p, 'w').write(s)
