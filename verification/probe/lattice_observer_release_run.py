#!/usr/bin/env python3
"""Owner-only execution under wine_lock.py; never builds or launches the game."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

import bottle
from lattice_observer_release_build import ROOT, digest
from lattice_observer_release_report import require, validate


def frozen(build):
    record = json.loads((build / 'build.json').read_text())
    require(record['exit_code'] == 0, 'failed build')
    exe = build / 'lattice_observer_release_fixture.exe'
    require(digest(exe) == record['exe_sha256'], 'EXE hash mismatch')
    require(not (build / 'd3d9.dll').exists(), 'standalone probe cannot load local proxy')
    for name, expected in record['inputs'].items():
        require(name in ('vertex.bin', 'candidate.bin'), 'unexpected retained input')
        require(digest(build / 'inputs' / name) == expected, 'input hash mismatch')
    require(set(record['inputs']) == {'vertex.bin', 'candidate.bin'}, 'input coverage')
    for name, expected in record['source_sha256'].items():
        require(digest(ROOT / name) == expected, 'source changed after build')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(os.environ.get('X3M_FIXTURE_BOTTLE') == 'X3', 'explicit X3 bottle required')
    build = args.build.resolve()
    record = frozen(build)
    info = bottle.describe()
    require(info['wine_arch'] == 'arm64' and
            info['environment'].get('FEX_X87REDUCEDPRECISION') == '1' and
            info['environment'].get('WINEMSYNC') == '1', 'unexpected X3 environment')
    require((bottle.bottle_dir() / 'dosdevices/z:').resolve() == Path('/'), 'Z: must map host root')
    args.output.mkdir(parents=True, exist_ok=False)
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b',
               str(build / 'lattice_observer_release_fixture.exe'),
               'Z:' + str(build / 'inputs').replace('/', '\\')]
    summary = dict(status='running', bottle=info, command=command,
                   exe_sha256=record['exe_sha256'], inputs=record['inputs'], game_launched=False,
                   native_windows='documented D3D9 API and cross-compilation only; not executed')
    begin = time.monotonic()
    try:
        observations = args.output / 'observations.jsonl'
        with observations.open('x') as stdout, (args.output / 'wine.log').open('x') as stderr:
            result = subprocess.run(command, stdout=stdout, stderr=stderr,
                                    env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=180)
        summary['exit_code'] = result.returncode
        require(result.returncode == 0, 'fixture failed; inspect observations.jsonl and wine.log')
        require(frozen(build) == record, 'build record changed during execution')
        require(observations.stat().st_size <= 4 * 1024 * 1024, 'oversize observations')
        summary.update(validate([json.loads(line) for line in observations.read_text().splitlines() if line.strip()]))
        summary['observations_sha256'] = digest(observations)
    except BaseException as error:
        summary.update(status='fail', error=repr(error))
        raise
    finally:
        summary['elapsed_seconds'] = time.monotonic() - begin
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n')
        print(json.dumps({key: summary[key] for key in ('status', 'elapsed_seconds')}))


if __name__ == '__main__':
    main()
