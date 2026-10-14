"""The "before B.6" build of scenario_t_b6.txt: the B.6 fixture source adapted to compile against the pre-B.6 core (73cea4aa), and nothing
else. Exactly four adaptations, each asserted:
  1. sb_b6_checks() is replaced by an empty stub (its units call gc::lap_law, gc::release_window and the 4-argument lap_speed, absent
     from the pre-B.6 core);
  2. sb_b6_review_checks() likewise (gc::petal_near, gc::escort_settled, Member::esc_in10);
  3. the fixture's lap-law comparison gc::lap_speed(*g, m, last_sb, t_rel) becomes the pre-B.6 5-argument call with gc::kEscortStageS
     (that core's escort stage point), so the "above the lap law" column of the before table compares against the old law;
  4. m.esc_in10 (2 uses in b6_escort_write) becomes false: the old core has no first-pass phase, so every post-launch escort frame is
     counted as before the pass (the "within 9.7 km without the pass noted" column is then not meaningful for the before table).
The scenario T code, its assertions and the B.6 measurement rows are otherwise identical to the after build.
Usage: python3 -I scenario_t_b6_pre_build.py <worktree>/verification/probe/ship_steering_host.cpp <out.cpp>
then, in a scratch tree holding git show 73cea4aa:src/proxy/ship_gate_ctl_core.h beside the other src/proxy headers of this commit:
  c++ -std=c++17 -O2 -Wall -Wextra -Werror <scratch>/verification/probe/ship_steering_host.cpp -o pre && ./pre --traffic 2>/dev/null |
  grep 'B\\.6:' > ship_steering_host_b6_pre_rows.txt"""
import sys

src, out = sys.argv[1], sys.argv[2]
s = open(src).read()
for name, nxt in (('static void sb_b6_checks() {', 'static void sb_b6_review_checks() {'),
                  ('static void sb_b6_review_checks() {', 'static void b5_checks() {')):
    a, b = s.index(name), s.index(nxt)
    s = s[:a] + name + '} // (stubbed for the pre-B.6 core)\n' + s[b:]
assert s.count('gc::lap_speed(*g, m, last_sb, t_rel)') == 1
s = s.replace('gc::lap_speed(*g, m, last_sb, t_rel)', 'gc::lap_speed(*g, m, last_sb, t_rel, gc::kEscortStageS)')
assert s.count('m.esc_in10') == 2
s = s.replace('m.esc_in10', 'false')
open(out, 'w').write(s)
