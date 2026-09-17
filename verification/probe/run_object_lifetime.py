#!/usr/bin/env python3
"""Fresh-build original object-lifetime ABI/exception/rollback regression."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

INPUTS = [
    'src/proxy/object_lifetime.cpp', 'src/proxy/object_lifetime.h',
    'src/proxy/engine_memory.cpp', 'src/proxy/engine_memory.h',
    'verification/probe/object_lifetime.cpp', 'verification/probe/build_object_lifetime.sh',
    'verification/probe/run_object_lifetime.py',
]
JOURNAL_CASES = ('no_consumer', 'retire_in_order', 'partial_drain', 'invalid_drain', 'flush_load_epoch',
                 'flush_registry_destroy', 'flush_registry_rebind', 'overflow_and_recovery', 'cost',
                 'reregistration_and_shutdown', 'flush_capacity_exhausted', 'saturated_registration')
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'


def run(root):
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def sources():
        return {name: digest(root / name) for name in INPUTS}

    results = bottle.results_dir(root)
    results.mkdir(parents=True, exist_ok=True)
    summary = results / 'object-lifetime-summary.json'
    data = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                game_launched=False, bottle=bottle.describe(), fresh_build=True, passed=False)
    # Invalidate any prior PASS before even reading source inputs. An interrupted
    # or failed invocation must never leave a previous result looking current.
    summary.write_text(json.dumps(data, indent=2) + '\n')
    try:
        data['sources_before'] = sources()
        exe = root / 'verification/probe/build/object_lifetime.exe'
        report = results / 'object-lifetime.txt'
        wine_log = results / 'object-lifetime-wine.log'
        build = subprocess.run(['sh', str(root / 'verification/probe/build_object_lifetime.sh')],
                               capture_output=True, timeout=60)
        (results / 'object-lifetime-build.txt').write_bytes(build.stdout + build.stderr)
        data.update(build_exit=build.returncode, sources_after_build=sources())
        if build.returncode == 0 and data['sources_before'] == data['sources_after_build']:
            data['executable_sha256'] = digest(exe)
            command = [WINE, '--bottle', bottle.BOTTLE, '--no-update', '--workdir', str(exe.parent), str(exe)]
            data['command'] = command
            with report.open('wb') as out, wine_log.open('wb') as err:
                data['exit_code'] = subprocess.run(command, stdout=out, stderr=err,
                                                  env=dict(os.environ), timeout=90).returncode
            data['executable_sha256_after_run'] = digest(exe)
            data['sources_after_run'] = sources()
            data['report_sha256'] = digest(report)
            lines = report.read_text().splitlines()
            terminal = re.compile(r'RESULT (PASS|FAIL) checks=(\d+) failures=(\d+) backend_calls=(\d+)')
            matches = [terminal.fullmatch(line) for line in lines if line.startswith('RESULT ')]
            match = matches[0] if len(matches) == 1 else None
            last = next((line for line in reversed(lines) if line.strip()), '')
            if match:
                data.update(checks=int(match[2]), failures=int(match[3]), backend_calls=int(match[4]))
            # Read-path evidence: per-mode cost (TIMING) and record identity (IDENTITY).
            def parse(line):
                return {k: v for k, v in (kv.split('=', 1) for kv in line.split()[1:])}
            data['read_path'] = {'timing': [parse(l) for l in lines if l.startswith('TIMING ')],
                                 'identity': [parse(l) for l in lines if l.startswith('IDENTITY ')]}
            # Retirement journal: measured cycle cost without/with a consumer and the empty drain.
            data['journal'] = [parse(l) for l in lines if l.startswith('JOURNAL ')]
            cases = [parse(l) for l in lines if l.startswith('JOURNAL_CASE ')]
            data['journal_cases'] = {c.get('name'): c.get('result') for c in cases}
            # x87 comparison fidelity: the FXSAVE/FXRSTOR round-trip control decides
            # whether the ST0-ST7 slots are compared bit-exactly or under the
            # documented significand tolerance. Recorded so a report from an exact
            # environment (native Windows) is distinguishable from a lossy one.
            x87 = [parse(l) for l in lines if l.startswith('X87 ')]
            data['x87'] = x87[0] if len(x87) == 1 else None
            data['x87_roundtrip_exact'] = bool(data['x87'] and data['x87'].get('roundtrip_exact') == '1')
            x87_reported = bool(data['x87'] and data['x87'].get('roundtrip_exact') in ('0', '1') and
                                {'save_stable', 'control_diff_slots', 'control_max_low_bits',
                                 'compare_diff_slots', 'compare_max_low_bits'} <= set(data['x87']))
            identical = (x87_reported and len(cases) == len(JOURNAL_CASES) and data['journal_cases'] == dict.fromkeys(JOURNAL_CASES, 'PASS') and
                         len(data['journal']) == 1 and
                         {'cycle_idle_us', 'cycle_journal_us', 'retirement_delta_us', 'empty_drain_us'} <= set(data['journal'][0]) and
                         len(data['read_path']['identity']) == 1 and data['read_path']['identity'][0].get('equal') == '1'
                         and len(data['read_path']['timing']) == 2)
            data['passed'] = bool(data['exit_code'] == 0 and match and match[0] == last and identical and
                                  match[1] == 'PASS' and data['failures'] == 0 and data['checks'] > 0 and
                                  data['sources_before'] == data['sources_after_run'] and
                                  data['executable_sha256'] == data['executable_sha256_after_run'])
    except Exception as error:
        data.update(passed=False, error=f'{type(error).__name__}: {error}')
    summary.write_text(json.dumps(data, indent=2) + '\n')
    return data


def main():
    data = run(Path(__file__).resolve().parents[2])
    print(json.dumps({k: v for k, v in data.items() if not k.startswith('sources_')}, indent=2))
    return 0 if data['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
