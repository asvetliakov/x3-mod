#!/usr/bin/env python3
"""Thin-vote pre-implementation census (taa-thin-geometry-alternatives.md section 3.2 / 4).

Two questions, from existing evidence only (no game launch):
  INDEX32: bodies of the merged-LOD census whose record 0 has more than 65,535 points. The subgroup
           builder (X3AP 0x004bb5d0..0x004bb6a2) compacts each group's referenced vertices and asks
           D3DXCreateMesh for 32-bit indices only above 0xffff of them, and CloneMesh adds option 1 on
           GetNumVertices > 0xffff (0x004bcbef), so a body at or below 65,535 record points has no INDEX32
           subset: the count is an upper bound; single-group bodies above the line are the lower bound.
  0x440:   the clone option is 0x440 (DEFAULT, WRITEONLY) unless the global configuration bit
           *(0x00606f34)+0x100 & 8 selects 0x660 (MANAGED) (0x004bcbca..0x004bcbe2): one value per session.
           The caster counter's shadow_replay_candidates rows classify the pools of every routed, z-writing,
           cascade-admitted draw's VB/IB (managed / default_pool / dynamic / unknown); summed per session.
Usage: census.py [session logs...]   (default: every session-*.log in the X3 capture directory)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CENSUS = ROOT / 'verification/results/lod-overlay-batch/census.txt'
CAPTURES = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures'

bodies = above = single = drawn_above = 0
for line in CENSUS.read_text().splitlines():
    points = re.search(r' r0_points=(\d+)', line)
    if not points:
        continue
    bodies += 1
    if int(points.group(1)) > 65535:
        above += 1
        groups = re.search(r' r0_groups=(\d+)', line)
        single += bool(groups and groups.group(1) == '1')
print(f'index32: census bodies={bodies} r0_points>65535={above} (upper bound) single_group>65535={single} (lower bound)')

logs = [Path(p) for p in sys.argv[1:]] or sorted(CAPTURES.glob('session-*.log'))
fields = ('managed', 'default_pool', 'dynamic', 'unknown')
sessions = 0
total = dict.fromkeys(fields, 0)
with_default = 0
for log in logs:
    sums = dict.fromkeys(fields, 0)
    rows = 0
    with open(log, 'rb') as handle:
        for raw in handle:
            if not raw.startswith(b'shadow_replay_candidates '):
                continue
            rows += 1
            text = raw.decode('ascii', 'replace')
            for name in fields:
                match = re.search(rf' {name}=(\d+)', text)
                if match:
                    sums[name] += int(match.group(1))
    if rows:
        sessions += 1
        with_default += sums['default_pool'] > 0
        for name in fields:
            total[name] += sums[name]
print(f'clone_0x440: sessions_with_counter={sessions} sessions_with_default_pool={with_default} routed_draw_frames '
      + ' '.join(f'{name}={total[name]}' for name in fields))
