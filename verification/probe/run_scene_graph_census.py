#!/usr/bin/env python3
"""Run the built scene-graph census CPU fixture in the X3 bottle and write a compact result record.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_scene_graph_census.py
Build first with build_scene_graph_census.py (which never runs Wine).
"""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
import bottle
ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'build/verification/scene-graph-census/scene_graph_census_fixture.exe'
OUT = ROOT / 'verification/results/scene-graph-census-cpu.json'


def fields(line):
    return {k: float(v) if re.fullmatch(r'-?[\d.]+', v) else v for k, v in (kv.split('=', 1) for kv in line.split()[3:])}


def main():
    name = os.environ.get('X3M_FIXTURE_BOTTLE')
    if name != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3')
    started = time.time()
    run = subprocess.run([bottle.WINE, *bottle.wine_args(name), str(EXE)], capture_output=True, text=True, timeout=600)
    lines = run.stdout.splitlines()
    total = re.search(r'SCENE GRAPH CENSUS CPU checks=(\d+) failures=(\d+)', run.stdout)
    pick = lambda prefix: next((fields(l) for l in lines if l.startswith(prefix)), None)
    record = {'fixture': str(EXE.relative_to(ROOT)), 'exit_status': run.returncode, 'elapsed_s': round(time.time() - started, 1),
              'checks': int(total.group(1)) if total else None, 'failures': int(total.group(2)) if total else None,
              'failure_lines': [l for l in lines if l.startswith('FAIL')],
              'walk': [fields(l) for l in lines if l.startswith('SCENE GRAPH WALK')], 'bound': pick('SCENE GRAPH BOUND'), 'insert': pick('SCENE GRAPH INSERT'), 'regions': pick('SCENE GRAPH REGIONS'),
              'address_space': pick('ADDRESS SPACE ROW'),
              'address_space_example': next((l.split(' ', 3)[3] for l in lines if l.startswith('ADDRESS SPACE EXAMPLE ')), None),
              'bottle': bottle.describe(name),
              'note': 'nodes are 0x270-byte process-heap blocks linked in a shuffled order; best of 5 per walk variant, '
                      'row_walk_us from the row itself; diagnostic timings, not game FPS'}
    OUT.write_text(json.dumps(record, indent=1) + '\n')
    print(json.dumps({k: record[k] for k in ('checks', 'failures', 'exit_status', 'walk', 'bound', 'insert', 'regions', 'address_space', 'failure_lines')}))
    if run.returncode != 0 and not total:
        print(run.stdout[-2000:], run.stderr[-2000:], file=sys.stderr)
    sys.exit(0 if run.returncode == 0 and total and record['failures'] == 0 else 1)


if __name__ == '__main__':
    main()
