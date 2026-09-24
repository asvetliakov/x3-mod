"""Shape refusal telemetry fixture rows (docs/verification/bolt-footprint.md, 2026-09-24).
Reads the newest session log of each run_motion_output.py bolt-shape case under verification/probe/build
(untracked) and prints the shape refusal row and the device-1 window row fields.
usage: python3 verification/results/bolt-footprint/shape_refusal_fixture.py (from the repository root)"""
import glob, re
kv = lambda l: dict(re.findall(r'(\w+)=([^ \n]+)', l))
for script in ('prims', 'decl'):
    dirs = sorted(glob.glob(f'verification/probe/build/motion-output-seam-ownership-bolt-shape-{script}-*'))
    logs = sorted(glob.glob(f'{dirs[-1]}/x3-modern-captures/session-*.log')) if dirs else []
    if not logs: print(script, 'no log'); continue
    rows = [l for l in open(logs[-1], errors='replace') if l.startswith('bolt_footprint')]
    refused = [kv(l) for l in rows if l.startswith('bolt_footprint_refused ')]
    windows = [kv(l) for l in rows if l.startswith('bolt_footprint ')]
    print(f'== {script} ({dirs[-1].rsplit("-", 3)[-3:]})')
    for r in refused: print('  refused', {k: r[k] for k in ('device', 'frame', 'reason', 'detail', 'primitives', 'stream0_bytes')})
    for w in windows: print('  window ', {k: w[k] for k in ('device', 'frames', 'draws', 'refused_shape', 'refused_rows', 'refused_max_prims', 'refused_shape_bits')})
