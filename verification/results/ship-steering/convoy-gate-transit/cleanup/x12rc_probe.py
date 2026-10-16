"""Cleanup 2026-10-16, item 3: is lane_frame's x12_rc branch (the loop-back join decided at a re-commit after an eviction) reachable on
main? Copies the tree's ship cores and fixture into OUT, prints a line to stderr where the branch decides and where it starts a loop,
builds (c++ -O2) and runs --lane; prints both counts. usage: python3 -I x12rc_probe.py ROOT OUT (no Wine, no game; about 1 minute)"""
import os, shutil, subprocess, sys
root, out = sys.argv[1], sys.argv[2]
os.makedirs(out + '/src/proxy', exist_ok=True)
os.makedirs(out + '/verification/probe', exist_ok=True)
for n in os.listdir(root + '/src/proxy'):
    if n.endswith('.h'):
        shutil.copy(root + '/src/proxy/' + n, out + '/src/proxy/' + n)
shutil.copy(root + '/verification/probe/ship_steering_host.cpp', out + '/verification/probe/ship_steering_host.cpp')
p = out + '/src/proxy/ship_gate_lane_core.h'
s = open(p).read()
old = '''        if (m.x12_rc && lane_laws(m)) { // (Run175 round: a body decides nothing; its flag waits)
            m.x12_rc = 0;'''
assert s.count(old) == 1
s = s.replace(old, old + '\n            std::fprintf(stderr, "X12RC_DECIDE\\n");')
old2 = '''                m.loop = 1;
                m.loop_conv = 0;
                m.loop_out = m.loop_sp = 0.0f;'''
assert s.count(old2) == 1, s.count(old2)
s = s.replace(old2, old2 + '\n                std::fprintf(stderr, "X12RC_LOOP\\n");')
s = '#include <cstdio>\n' + s
open(p, 'w').write(s)
r = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', out + '/verification/probe/ship_steering_host.cpp', '-o', out + '/host'],
                   capture_output=True, text=True)
if r.returncode:
    sys.exit(r.stderr[-2000:])
run = subprocess.run([out + '/host', '--lane'], capture_output=True, text=True)
print('--lane: x12_rc decisions %d, loops started %d; %s' % (run.stderr.count('X12RC_DECIDE'), run.stderr.count('X12RC_LOOP'),
      [l for l in run.stdout.splitlines() if l.startswith('ship_steering_host checks=')][-1]))
for l in run.stderr.splitlines():
    if l.startswith('FAIL'):
        print('  ' + l[:200])  # (the instrumented build's stderr writes slow the host-timing bench)
