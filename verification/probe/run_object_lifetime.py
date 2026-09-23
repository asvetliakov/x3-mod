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
# The fixtures' FNV fold seed: a record equal to it means nothing was folded.
SEED_RECORD = f'{1469598103934665603:016x}'


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
            # Recorded repo-relative so the committed summary does not carry the checkout's path.
            data['command'] = command[:5] + [str(exe.parent.relative_to(root)), str(exe.relative_to(root))]
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
            # Read-path evidence: one TIMING line for the validated direct reads, the
            # only mode since 2026-09-22 (the rpm A/B mode and its IDENTITY
            # comparison were removed), carrying the folded lifetime record.
            def parse(line):
                return {k: v for k, v in (kv.split('=', 1) for kv in line.split()[1:])}
            data['read_path'] = {'timing': [parse(l) for l in lines if l.startswith('TIMING ')]}
            timing = data['read_path']['timing']
            read_path_reported = (len(timing) == 1 and timing[0].get('mode') == 'direct'
                                  and {'snapshot_us', 'reads_per_call', 'queries_per_call'} <= set(timing[0])
                                  and timing[0].get('record') not in (None, SEED_RECORD, '0' * 16))
            data['read_path_reported'] = read_path_reported
            # Retirement journal: measured cycle cost without/with a consumer and the empty drain.
            data['journal'] = [parse(l) for l in lines if l.startswith('JOURNAL ')]
            cases = [parse(l) for l in lines if l.startswith('JOURNAL_CASE ')]
            # Engine-reader modes (engine_memory.h): the hooked cycle's cost with frames advancing,
            # stalled past the 250 ms bound, and under the shutdown signal.
            data['read_modes'] = [parse(l) for l in lines if l.startswith('READ_MODES ')]
            read_modes_reported = (len(data['read_modes']) == 1 and
                                   {'cycle_frame_us', 'cycle_stalled_us', 'cycle_shutdown_us', 'queries_frame',
                                    'queries_stalled', 'queries_shutdown'} <= set(data['read_modes'][0]))
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
            identical = (x87_reported and read_modes_reported and len(cases) == len(JOURNAL_CASES) and data['journal_cases'] == dict.fromkeys(JOURNAL_CASES, 'PASS') and
                         len(data['journal']) == 1 and
                         {'cycle_idle_us', 'cycle_journal_us', 'retirement_delta_us', 'empty_drain_us'} <= set(data['journal'][0]) and
                         read_path_reported)
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
