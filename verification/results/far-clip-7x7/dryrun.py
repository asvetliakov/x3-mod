#!/usr/bin/env python3
"""Launcher --dry-run of the Run 84 A stand command (docs/verification/user-runs.md, the x3run line) with the worktree's DLL,
default and with --taa-far-clip 3x3; prints the far-clip / far-stabiliser variables each sends. Never launches the game.
Run: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/results/far-clip-7x7/dryrun.py [DLL]"""
import json, os, shlex, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
line = next(l for l in (ROOT / 'docs/verification/user-runs.md').read_text().splitlines() if '/x3run --direct' in l)
stand = shlex.split(line[line.index('/x3run ') + len('/x3run '):])
dll = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'build/d3d9.dll')
env = {k: v for k, v in os.environ.items() if not k.startswith('X3M_') and k != 'CX_DEBUGMSG'}
env['X3M_MOTION_FRAME_LOG'] = '1'
names = ('X3M_TAA_FAR_CLIP', 'X3M_TAA_FAR_CLIP_DEFAULT', 'X3M_TAA_FAR_STABILISER', 'X3M_TAA_FAR_GATE', 'X3M_TAA_FAR_GATE_DEFAULT')
for label, extra in (('default', []), ('far_clip_3x3', ['--taa-far-clip', '3x3']), ('vanilla', None)):
    args = ['--vanilla'] if extra is None else stand + extra
    run = subprocess.run([sys.executable, 'tools/manage.py', 'launch', '--dry-run', '--bottle', 'X3', '--dll-source', dll, *args],
                         cwd=ROOT, env=env, capture_output=True, text=True)
    if run.returncode:
        print(label, 'exit=%d' % run.returncode, run.stderr.strip().splitlines()[-1:])
        continue
    sent = json.loads(run.stdout[run.stdout.index('{'):])['env']
    print(label, 'exit=0 args=%d' % len(args), ' '.join('%s=%s' % (n, sent.get(n)) for n in names))
