#!/usr/bin/env python3
"""Shader instruction-slot budget probe: one Wine run of shader_slot_budget_fixture.exe.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_shader_slot_budget.py

Builds the fixture with i686-w64-mingw32-g++ into build/shader_slot_budget/ (untracked), runs it
once against the bottle's builtin d3d9 (d3d9=b, the backend the proxy forwards to) and the game's
d3dx9_37.dll (d3dx9_37=n, loaded by path), and writes
  verification/results/bottle-<bottle>/shader-slot-budget.json  (bottle, caps, one row per case)
  verification/results/shader-slot-budget/summary.md             (compact table)
The full stdout/stderr stays beside the executable. The JSON is strict: an unread pixel or any non-finite
number is null. `--reparse` rebuilds both files from the kept stdout/stderr and the run metadata of the
existing JSON without running Wine (no lock needed). `--first-run-log` merges an earlier run's first-draw times
(`first_draw_ms_first_run`, matched by stage/kind/N/loop) and checks its words/slots/HRESULTs/executed counts equal
this run's: the first run to meet a program pays the backend's cold shader compile, later runs of identical
bytecode hit its cache. Never launches the game.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import math
import bottle

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'verification/probe/shader_slot_budget_fixture.cpp'
BUILD = ROOT / 'build/shader_slot_budget'
EXE = BUILD / 'shader_slot_budget_fixture.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++']
TEXT_KEYS = {'text', 'path', 'description', 'driver', 'stage', 'kind', 'reason', 'status', 'name', 'd3d9', 'd3dx'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(text):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', text):
        if value == 'null':
            out[key] = None
        elif key in TEXT_KEYS or key.endswith('hr') or key.endswith('caps') or key.endswith('version') or key in ('vendor', 'device', 'coop', 'behavior'):
            out[key] = value
        elif re.fullmatch(r'-?\d+', value):
            out[key] = int(value)
        else:
            try:
                number = float(value)
                out[key] = number if math.isfinite(number) else None
            except ValueError:
                out[key] = value
    return out


def parse(stdout):
    report = {'cases': [], 'errors': [], 'skipvalidation': [], 'modules': [], 'other': []}
    for line in stdout.splitlines():
        tag, _, rest = line.partition(' ')
        if tag == 'CASE':
            report['cases'].append(fields(rest))
        elif tag == 'ERROR':
            report['errors'].append(fields(rest))
        elif tag == 'SKIPVALIDATION':
            report['skipvalidation'].append(fields(rest))
        elif tag == 'MODULE':
            report['modules'].append(fields(rest))
        elif tag in ('CAPS', 'ADAPTER', 'DEVICE', 'RESULT', 'HLSLSTOP'):
            report[tag.lower()] = fields(rest)
        elif line.strip():
            report['other'].append(line[:200])
    return report


def row(case, errors):
    created = case.get('create_hr') == '00000000'
    err = next((e['text'] for e in errors if (e['stage'], e['kind'], e['n'], e['loop']) ==
                (case['stage'], case['kind'], case['n'], case['loop'])), '')
    executed = case.get('executed')
    cold, warm = case.get('first_draw_ms_first_run'), case.get('first_draw_ms')
    return '| {stage} | {kind} | {n}{loop} | {compile} | {slots} | {create} | {draw} | {executed} | {ms} | {cold} | {warm} |'.format(
        cold=f'{cold:.1f}' if created and isinstance(cold, (int, float)) and cold >= 0 else '-',
        warm=f'{warm:.1f}' if created and isinstance(warm, (int, float)) and warm >= 0 else '-',
        stage=case['stage'], kind=case['kind'], n=case['n'], loop=f' x{case["loop"]}' if case['loop'] else '',
        compile=case['compile_hr'] + (f' `{err[:90]}`' if err else ''), slots=case.get('slots', -1),
        create=case.get('create_hr', '-'), draw=case.get('draw_hr', '-') if created else '-',
        executed=(f'{executed:.0f}/{case["executed_expected"]}' if isinstance(executed, (int, float)) and executed >= 0 else '-') if created else '-',
        ms=(f'{case["ms_per_draw"]:.3f}' if created and (case.get('ms_per_draw') or -1) >= 0 else '-'))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--max-hlsl', type=int, default=16384, help='Largest straight-line HLSL N to compile')
    parser.add_argument('--limit-s', type=int, default=2400)
    parser.add_argument('--reparse', action='store_true', help='Rebuild the JSON and summary from the kept log; no Wine')
    parser.add_argument('--first-run-log', type=Path, help='stdout of an earlier (cold-cache) run of the same programs')
    args = parser.parse_args()
    global FIRST_RUN_LOG
    FIRST_RUN_LOG = args.first_run_log
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    out_path, err_path = BUILD / 'shader-slot-budget.stdout', BUILD / 'shader-slot-budget.stderr'
    out_json = bottle.results_dir(ROOT) / 'shader-slot-budget.json'
    if args.reparse:
        previous = json.loads(out_json.read_text(), parse_constant=lambda name: None)
        record = {k: v for k, v in previous.items() if k not in ('report', 'stderr_lines', 'stderr_err_lines', 'passed')}
        if sha(EXE) != record['executable_sha256']:
            sys.exit('the kept executable is not the one that produced the log')
        record['reparsed'] = {'runner_sha256': sha(__file__), 'fixture_source_sha256_now': sha(SOURCE)}
        return finish(record, out_path, err_path, out_json)
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(['i686-w64-mingw32-g++', *FLAGS, str(SOURCE), '-o', str(EXE), '-luser32'], check=True)
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    overrides = 'd3d9=b;d3dx9_37=n'
    command = [bottle.WINE, *bottle.wine_args(), '--dll', overrides, '--workdir', str(BUILD),
               str(EXE), 'Z:' + str(d3dx).replace('/', '\\'), str(args.max_hlsl)]
    record = {"max_hlsl": args.max_hlsl, "bottle": bottle.describe(), 'game_launched': False, 'source_sha256': sha(SOURCE),
              'runner_sha256': sha(__file__), 'executable_sha256': sha(EXE), 'd3dx9_37': str(d3dx),
              'd3dx9_37_sha256': sha(d3dx), 'dll_overrides': overrides, 'command': command}
    started = time.time()
    with open(out_path, 'w') as out, open(err_path, 'w') as err:
        try:
            record['exit_code'] = subprocess.run(command, stdout=out, stderr=err, cwd=str(BUILD), timeout=args.limit_s,
                                                 env=dict(os.environ, WINEDLLOVERRIDES=overrides)).returncode
        except subprocess.TimeoutExpired:
            record['exit_code'] = 'timeout'
            subprocess.run(['pkill', '-f', EXE.name], check=False)
    record['elapsed_s'] = round(time.time() - started, 1)
    assert sha(SOURCE) == record['source_sha256'], 'fixture source changed during the run'
    return finish(record, out_path, err_path, out_json)


FIRST_RUN_LOG = None
COMPARED = ('words', 'slots', 'compile_hr', 'create_hr', 'draw_hr', 'executed')


def merge_first_run(record, report):
    if not FIRST_RUN_LOG:
        return
    first = {(c['stage'], c['kind'], c['n'], c['loop']): c
             for c in parse(FIRST_RUN_LOG.read_text(errors='replace'))['cases'] if 'first_draw_ms' in c}
    mismatched = []
    for case in report['cases']:
        other = first.get((case['stage'], case['kind'], case['n'], case['loop']))
        if other:
            case['first_draw_ms_first_run'] = other['first_draw_ms']
            if any(other.get(f) != case.get(f) for f in COMPARED):
                mismatched.append([case['stage'], case['kind'], case['n'], case['loop']])
    record['first_run'] = {'log': str(FIRST_RUN_LOG), 'sha256': sha(FIRST_RUN_LOG), 'cases_with_first_draw': len(first),
                           'matched': sum('first_draw_ms_first_run' in c for c in report['cases']), 'mismatched': mismatched,
                           'note': 'first run = first time the backend compiled these programs (cold); the main run '
                                   'repeated identical bytecode (warm backend cache, inferred)'}


def finish(record, out_path, err_path, out_json):
    report = parse(out_path.read_text(errors='replace'))
    merge_first_run(record, report)
    stderr = err_path.read_text(errors='replace').splitlines()
    record['stderr_lines'] = len(stderr)
    record['stderr_err_lines'] = sorted({l.strip()[:200] for l in stderr if 'err:' in l})[:20]
    record['report'] = report
    record['passed'] = record['exit_code'] == 0 and 'result' in report
    out_json.write_text(json.dumps(record, indent=1, allow_nan=False) + '\n')

    caps = report.get('caps', {})
    md = ['# Shader instruction-slot budget', '',
          f'Bottle: {bottle.label()}. Adapter: {report.get("adapter", {}).get("description")}. '
          f'd3d9: {", ".join(m.get("path", "") for m in report["modules"])}. d3dx9_37 sha256 {record["d3dx9_37_sha256"][:8]}.',
          f'Produced by `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_shader_slot_budget.py` '
          f'(fixture `verification/probe/shader_slot_budget_fixture.cpp` sha256 {record["source_sha256"][:8]}); exit {record["exit_code"]}, '
          f'{record["elapsed_s"]} s. Full record: `verification/results/bottle-{bottle.BOTTLE}/shader-slot-budget.json`.', '',
          '## Caps', '', '| field | value |', '| --- | --- |']
    md += [f'| {k} | {v} |' for k, v in caps.items()]
    md += ['', '## Cases', '',
           'N = mads in the chain (`x x L` = [loop] of L iterations). Executed = (g - 1) * 65536 read back at the centre pixel '
           '/ expected. ms = per full-screen draw at 512x512 A32B32G32R32F (event-query sync); raw chains read only constants, '
           'so their value is uniform and their draw time is not per-pixel ALU cost. `asm` = D3DXAssembleShader, created only. '
           'Slots = D3DXDisassembleShader "approximately N instruction slots used". First draw = draw + sync of the first draw '
           'with the program, dominated by the backend shader compile: "cold" from the first run (`--first-run-log`, '
           'the first time the backend met these programs), "warm" from this run of identical bytecode (backend cache, inferred).', '',
           '| stage | kind | N | compile hr | slots | create hr | draw hr | executed | ms/draw | first draw ms cold | first draw ms warm |',
           '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
    md += [row(c, report['errors']) for c in report['cases']]
    if report['skipvalidation']:
        md += ['', 'Retries with D3DXSHADER_SKIPVALIDATION after a compile failure:', '']
        md += [f'- {s["stage"]} N={s["n"]} loop={s["loop"]}: hr {s["hr"]}' + (f', slots {s["slots"]}' if 'slots' in s else '') +
               (f', `{s["text"][:120]}`' if 'text' in s else '') for s in report['skipvalidation']]
    if record.get('first_run'):
        f = record['first_run']
        md += ['', f'First run log sha256 {f["sha256"][:8]}: {f["matched"]} cases matched, mismatched in words/slots/HRESULTs/executed: '
               f'{f["mismatched"] or "none"}.']
    if 'hlslstop' in report:
        md += ['', f'HLSL sweep stopped at N={report["hlslstop"].get("n")}: {report["hlslstop"].get("reason")}.']
    summary = ROOT / 'verification/results/shader-slot-budget/summary.md'
    summary.parent.mkdir(parents=True, exist_ok=True)
    summary.write_text('\n'.join(md) + '\n')
    print(json.dumps({'passed': record['passed'], 'exit_code': record['exit_code'], 'elapsed_s': record['elapsed_s'],
                      'cases': len(report['cases']), 'caps': caps, 'json': str(out_json), 'summary': str(summary)}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
