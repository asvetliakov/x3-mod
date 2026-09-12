#!/usr/bin/env python3
"""Replay local game meshes through verified/native-fast service boundaries.

Run after run_loading_trace.py in the same bottle, under wine_lock.py. Reuses
its exact freshly verified fixture bytes, so this cannot overwrite that suite's
executable provenance. Raw meshes and verbose native differences stay in /tmp.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile

import bottle
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump-dir', type=Path, required=True)
    parser.add_argument('--expected-count', type=int, default=37)
    args = parser.parse_args()
    results = bottle.results_dir(ROOT)
    destination = results / 'mesh-adjacency-replay-summary.json'
    report = {'passed': False, 'bottle': bottle.describe(), 'game_launched': False}
    destination.write_text(json.dumps(report, indent=2) + '\n')
    try:
        assert not game_running(), 'X3AP is running'
        assert args.expected_count > 0
        dumps = sorted(args.dump_dir.resolve().glob('mesh-adjacency-*.bin'))
        assert len(dumps) == args.expected_count, 'Unexpected local dump count'
        source_report = results / 'loading-trace-mesh-summary.json'
        previous = json.loads(source_report.read_text())
        assert previous['passed'] and previous['sources_and_binaries_unchanged'], 'Run current loading suite first'
        source_map = previous['sources_after_run']
        assert source_map == previous['sources_before_build']
        assert all(sha(ROOT / name) == digest for name, digest in source_map.items()), 'Stale compiled source'
        directory = ROOT / 'verification/probe/build/mesh_adjacency_fast'
        binary = directory / 'mesh_adjacency_fast_fixture.exe'
        recorded = previous['cases']['mesh-adjacency-cache-on']
        assert sha(binary) == recorded['executable_sha256'], 'Fixture overwritten since verified suite'
        assert all(sha(directory / name) == digest for name, digest in recorded['dll_sha256'].items()), 'Fixture DLL changed'
        native = bottle.game_dir() / 'd3dx9_37.dll'
        assert sha(native) == previous['native_after'] == previous['native_before'], 'Native provenance changed'
        inputs = {str(p): sha(p) for p in dumps}
        output = Path(tempfile.mkdtemp(prefix='x3-adjacency-safe-replay-'))
        stdout, stderr = output / 'stdout.txt', output / 'stderr.txt'
        command = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
                   '--bottle', bottle.BOTTLE, '--no-update', '--dll', 'd3dx9_37=n,b;d3d9=b',
                   '--workdir', str(directory), str(binary), 'replay-safe']
        command += ['Z:' + str(p).replace('/', '\\') for p in dumps]
        report.update(sources=source_map, suite_summary_sha256=sha(source_report),
                      runner_sha256=sha(Path(__file__)), executable_sha256=sha(binary),
                      dll_sha256=recorded['dll_sha256'], native_sha256=sha(native), inputs=inputs,
                      raw_output_directory=str(output))
        with stdout.open('w') as out, stderr.open('w') as err:
            run = subprocess.run(command, cwd=directory, stdout=out, stderr=err, timeout=1200,
                                 env=dict(os.environ, X3M_TELEMETRY='1', X3M_ADMISSION='0',
                                          X3M_CRYPT_CACHE='0', X3M_GZ_BUFFER='0', X3M_LOADING_PROBES='0',
                                          X3M_MESH_CACHE='0', X3M_RESOURCE_READ='native', X3M_DAT_HANDLES='0'))
        rows, terminals = [], []
        failed = False
        with stdout.open() as stream:
            for line in stream:
                failed |= 'FAIL' in line
                if line.startswith('REPLAY_ADMISSION '):
                    rows.append(dict(re.findall(r'(\w+)=([^\s]+)', line)))
                if line.startswith('MESH ADJACENCY REPLAY '):
                    terminals.append(dict(re.findall(r'(\w+)=([^\s]+)', line)))
        assert run.returncode == 0 and not failed, 'Fixture failed; inspect private raw output'
        assert len(rows) == len(dumps) and len({r['file'] for r in rows}) == len(dumps)
        assert {r['file'] for r in rows} == {p.name for p in dumps}
        assert all(r['equal_native'] == '1' and r['faults'] == '0' for r in rows)
        assert all(math.isfinite(float(r[k])) and float(r[k]) >= 0 for r in rows for k in ('native_us', 'served_us'))
        assert all(int(r['computed']) + int(r['fallback_normals']) + int(r['fallback_fp']) == 1 for r in rows), 'Missing admission coverage'
        assert len(terminals) == 1 and all(terminals[0][k] == v for k, v in
            {'dumps': str(len(dumps)), 'equal': str(len(dumps)), 'mismatched': '0', 'unreadable': '0', 'failures': '0'}.items())
        assert all(sha(ROOT / name) == digest for name, digest in source_map.items())
        assert sha(binary) == report['executable_sha256'] and sha(native) == report['native_sha256']
        assert all(sha(directory / name) == digest for name, digest in recorded['dll_sha256'].items())
        assert inputs == {str(p): sha(p) for p in dumps} and sha(source_report) == report['suite_summary_sha256']
        assert sha(Path(__file__)) == report['runner_sha256']
        report.update(passed=True, exit_code=run.returncode, cases=rows, terminal=terminals[0],
                      stdout_sha256=sha(stdout), stderr_sha256=sha(stderr), inputs_unchanged=True)
    except (Exception, KeyboardInterrupt) as error:
        report['error'] = repr(error)
    destination.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report.get(k) for k in ('passed', 'error', 'terminal', 'raw_output_directory')}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
