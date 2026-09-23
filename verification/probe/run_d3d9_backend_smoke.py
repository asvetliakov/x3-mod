#!/usr/bin/env python3
"""D3D9 backend smoke probe: one Wine run of d3d9_backend_smoke_fixture.exe against one d3d9.dll.

Invoke only as:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_d3d9_backend_smoke.py --d3d9 <path|builtin>

`--d3d9 builtin` loads the bottle's d3d9 by name (wined3d, override d3d9=b); a path loads that file
(override d3d9=n, so Wine does not substitute its builtin). The game's own d3dx9_37.dll is loaded by
path (d3dx9_37=n) for D3DXAssembleShader / D3DXCreateEffect. The 211 programs of the game's `3_0`
shader directories (verification/results/shader-sweep-aliases.json) are read from the local, untracked
extraction under /tmp/x3-shader-sweep/programs; when that is absent the sweep is skipped and said so.
DXVK_LOG_LEVEL=info and MVK_CONFIG_LOG_LEVEL=2 are set; --env adds process variables, --wine-env passes
variables through CrossOver's `wine --env` (applied after the bottle's environment, so
CX_GRAPHICS_BACKEND=dxvk selects CrossOver's bundled DXVK for one run; DYLD_* set this way are still
dropped by the hardened Wine loader, so MoltenVK cannot be swapped per process).

Writes <results>/d3d9-backend-smoke/<name>.json (bottle, command, provenance hashes, parsed report,
stderr attribution per swept program) and <name>.txt (stdout without passing sweep rows, relevant
stderr lines, trimmed); the full stdout/stderr stays beside the fixture executable (untracked build
directory). Never builds, never launches the game.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import time
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / 'build/d3d9_backend_smoke_fixture.exe'
SOURCES = ('verification/probe/d3d9_backend_smoke_fixture.cpp', 'verification/probe/run_d3d9_backend_smoke.py')
ALIASES = ROOT / 'verification/results/shader-sweep-aliases.json'
PROGRAMS = Path('/tmp/x3-shader-sweep/programs')
ERROR = re.compile(r'(^|\s|:)err:|\[mvk-error\]|\berror\b|failed|VK_ERROR', re.IGNORECASE)
RELEVANT = re.compile(r'err:|warn:|info:|\[mvk-|DXVK|MoltenVK|Vulkan|Metal|wined3d|d3d9|adapter|driver|fixme:d3d', re.IGNORECASE)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def windows_path(path):
    return 'Z:' + str(path).replace('/', '\\')


HEX_KEYS = ('behavior', 'vendor', 'device', 'version')  # printed as hex: text even when every digit is decimal


def fields(text):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', text):
        out[key] = int(value) if re.fullmatch(r'-?\d+', value) and not key.endswith('hr') and key not in HEX_KEYS else value
    return out


def parse(stdout):
    report = {'modules': [], 'steps': [], 'formats': [], 'shaders': [], 'draws': [], 'queries': [], 'resz': [],
              'checks': [], 'sweep': [], 'other': []}
    tags = {'MODULE': 'modules', 'STEP': 'steps', 'FORMAT': 'formats', 'SHADER': 'shaders', 'DRAW': 'draws',
            'QUERY': 'queries', 'RESZ': 'resz', 'SWEEP': 'sweep', 'ASSEMBLE': 'other'}
    for line in stdout.splitlines():
        tag, _, rest = line.partition(' ')
        if tag == 'CHECK':
            label, _, verdict = rest.rpartition(' ')
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif tag == 'STEP':
            name, _, rest = rest.partition(' ')
            report['steps'].append(dict(fields(rest), name=name))
        elif tag in tags:
            report[tags[tag]].append(fields(rest))
        elif tag in ('ADAPTER', 'CAPS', 'DEVICE', 'EFFECT', 'PRESENT', 'STRETCHDEPTH', 'SWEEPSUMMARY', 'SWEEPLIST', 'SWEEPSTOP', 'RESULT'):
            report[tag.lower()] = fields(rest)
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def attribute(stderr):
    """Split stderr at the fixture's sweep markers: error lines per swept program, and the rest."""
    per_program, outside, current = {}, [], None
    for line in stderr.splitlines():
        begin = re.match(r'X3M-SWEEP-BEGIN (\d+) (\S+)', line)
        if begin:
            current = (int(begin.group(1)), begin.group(2))
            continue
        if line.startswith('X3M-SWEEP-END'):
            current = None
            continue
        if current is None:
            outside.append(line)
        elif ERROR.search(line):
            per_program.setdefault(current, []).append(line.strip()[:300])
    return per_program, outside


def live_program_list(path):
    aliases = json.loads(ALIASES.read_text())['effects']
    live = sorted({program for effect in aliases if '/3_0/' in effect['path'] for program in effect['program_occurrences']})
    missing = [p for p in live if not (PROGRAMS / f'{p}.bin').is_file()]
    if missing:
        return None, f'{len(missing)} of {len(live)} live programs missing under {PROGRAMS}'
    path.write_text(''.join(f'{p[:2]} {windows_path(PROGRAMS / (p + ".bin"))}\n' for p in live))
    return live, None


def run_watched(command, environment, exe, record, limit_s=1800, after_result_s=60):
    """Run with stdout/stderr in files; a fixture still alive 60 s after its RESULT line (a hang at
    process exit) or past the limit is killed by executable name and recorded as such."""
    out_path, err_path = exe.parent / 'd3d9-backend-smoke.stdout', exe.parent / 'd3d9-backend-smoke.stderr'
    with open(out_path, 'w') as out, open(err_path, 'w') as err:
        process = subprocess.Popen(command, stdout=out, stderr=err, env=environment, cwd=str(exe.parent))
        started, result_at, killed = time.time(), None, None
        while process.poll() is None:
            time.sleep(2)
            if result_at is None and '\nRESULT ' in out_path.read_text(errors='replace'):
                result_at = time.time()
            late = (result_at and time.time() - result_at > after_result_s) or time.time() - started > limit_s
            if late and not killed:
                killed = 'hang_after_result' if result_at else 'timeout'
                subprocess.run(['pkill', '-f', exe.name], check=False)
            if killed and time.time() - started > limit_s + 60:
                process.kill()
        record['killed'] = killed
    return out_path.read_text(errors='replace'), err_path.read_text(errors='replace'), process.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--d3d9', required=True, help='d3d9.dll to load (unix path), or "builtin" for the bottle d3d9 (wined3d)')
    parser.add_argument('--name', help='Result name (default: wined3d for builtin, else the DLL\'s parent directory name)')
    parser.add_argument('--exe', type=Path, default=DEFAULT_EXE, help='The CMake-built d3d9_backend_smoke_fixture.exe')
    parser.add_argument('--no-sweep', action='store_true', help='Skip the game-program sweep')
    parser.add_argument('--env', action='append', default=[], help='KEY=VALUE for the process environment (repeatable)')
    parser.add_argument('--wine-env', action='append', default=[], help='KEY=VALUE passed through `wine --env` (repeatable)')
    parser.add_argument('--d3d9-order', help='d3d9 load order override (default b for builtin, n for a path; a Wine-builtin-marked '
                        'file such as CrossOver\'s DXVK needs b)')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        sys.exit('set X3M_FIXTURE_BOTTLE=X3 and run through verification/probe/wine_lock.py')
    exe = args.exe.resolve()
    if not exe.is_file():
        sys.exit(f'fixture not built: {exe} (cmake --build <dir> --target d3d9_backend_smoke_fixture)')
    beside = [n for n in ('d3d9.dll', 'd3dx9_37.dll', 'dxgi.dll', 'vulkan-1.dll') if (exe.parent / n).exists()]
    if beside:
        sys.exit(f'{", ".join(beside)} beside the fixture would be loaded first (application directory); build in an empty directory')
    builtin = args.d3d9 == 'builtin'
    d3d9 = None if builtin else Path(args.d3d9).resolve()
    if d3d9 and not d3d9.is_file():
        sys.exit(f'no such d3d9: {d3d9}')
    name = args.name or ('wined3d' if builtin else re.sub(r'[^\w.-]+', '-', d3d9.parent.name))
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    results = bottle.results_dir(ROOT) / 'd3d9-backend-smoke'
    results.mkdir(parents=True, exist_ok=True)
    overrides = f'd3d9={args.d3d9_order or ("b" if builtin else "n")};d3dx9_37=n'
    environment = dict(os.environ, WINEDLLOVERRIDES=overrides, DXVK_LOG_LEVEL='info', DXVK_LOG_PATH='none', MVK_CONFIG_LOG_LEVEL='2')
    environment.update(item.split('=', 1) for item in args.env)
    record = {'passed': False, 'game_launched': False, 'name': name, 'bottle': bottle.describe(),
              'sources': {s: sha(ROOT / s) for s in SOURCES}, 'executable': str(exe), 'executable_sha256': sha(exe),
              'd3d9': str(d3d9) if d3d9 else 'builtin', 'd3d9_sha256': sha(d3d9) if d3d9 else None,
              'd3d9_bytes': d3d9.stat().st_size if d3d9 else None, 'd3dx9_37': str(d3dx), 'd3dx9_37_sha256': sha(d3dx),
              'dll_overrides': overrides, 'env': {k: environment[k] for k in ('DXVK_LOG_LEVEL', 'MVK_CONFIG_LOG_LEVEL')},
              'extra_env': args.env, 'wine_env': args.wine_env}
    list_arg = '-'
    if not args.no_sweep:
        live, reason = live_program_list(exe.parent / 'd3d9-backend-smoke-programs.txt')
        record['sweep_programs'] = len(live) if live else 0
        record['sweep_skipped'] = reason
        if live:
            list_arg = windows_path(exe.parent / 'd3d9-backend-smoke-programs.txt')
    moltenvk = Path(bottle.WINE).parents[1] / 'lib/aarch64/libMoltenVK.dylib'  # what win32u.so dlopens (leaf name, rpath)
    record['moltenvk'] = {'path': str(moltenvk), 'bytes': moltenvk.stat().st_size, 'sha256': sha(moltenvk)} if moltenvk.is_file() else None
    command = [bottle.WINE, *bottle.wine_args(), '--dll', overrides, '--workdir', str(exe.parent)]
    if args.wine_env:
        command += ['--env', ' '.join(shlex.quote(item) for item in args.wine_env)]
    command += [str(exe), windows_path(d3d9) if d3d9 else 'builtin', windows_path(d3dx), list_arg]
    record['command'] = command
    out_json = results / f'{name}.json'
    try:
        started = time.time()
        stdout, stderr, record['exit_code'] = run_watched(command, environment, exe, record)
        record['elapsed_s'] = round(time.time() - started, 1)
        full = exe.parent / f'd3d9-backend-smoke-{name}.full.txt'
        full.write_text(stdout + '\n--- stderr ---\n' + stderr)
        record['full_log'] = str(full)
        report = parse(stdout)
        per_program, outside = attribute(stderr)
        swept = {row.get('name'): row for row in report['sweep']}
        report['sweep_error_programs'] = len(per_program)
        report['sweep_errors'] = [{'index': index, 'name': program, 'kind': program[:2], 'lines': len(lines), 'first': lines[0],
                                   'create_hr': swept.get(program, {}).get('create_hr')}
                                  for (index, program), lines in sorted(per_program.items())]
        errors_outside = [line.strip()[:300] for line in outside if ERROR.search(line)]
        report['stderr_errors_outside_sweep'] = len(errors_outside)
        report['first_error'] = next(iter(errors_outside), None) or (report['sweep_errors'][0]['first'] if report['sweep_errors'] else None)
        report['moltenvk_lines'] = sorted({line.strip()[:200] for line in stderr.splitlines() if 'MoltenVK' in line or 'mvk' in line.lower()})[:12]
        record['report'] = report
        relevant = [line.rstrip()[:300] for line in outside if RELEVANT.search(line)]
        text = [f'# {bottle.label()}', f'# d3d9={record["d3d9"]} sha256={record["d3d9_sha256"]} overrides={overrides}',
                f'# command: {" ".join(shlex.quote(c) for c in command)}', f'# exit={record["exit_code"]} elapsed_s={record["elapsed_s"]}', '']
        text += [line for line in stdout.splitlines() if not (line.startswith('SWEEP ') and 'create_hr=00000000 draw_hr=00000000' in line)]
        text += ['', f'--- stderr outside the sweep: {len(relevant)} relevant of {len(outside)} lines (first 150) ---'] + relevant[:150]
        text += ['', f'--- stderr error lines inside the sweep: {len(per_program)} programs (first 60, first line each) ---']
        text += [f'{e["index"]} {e["name"]} lines={e["lines"]} {e["first"]}' for e in report['sweep_errors'][:60]]
        (results / f'{name}.txt').write_text('\n'.join(text) + '\n')
        record['report_sha256'] = sha(results / f'{name}.txt')
        assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES} and sha(exe) == record['executable_sha256'], 'Provenance changed during run'
        record['passed'] = record['exit_code'] == 0 and report.get('result', {}).get('failed') == 0
    finally:
        out_json.write_text(json.dumps(record, indent=1) + '\n')
        report = record.get('report', {})
        print(json.dumps({'name': name, 'passed': record['passed'], 'exit_code': record.get('exit_code'), 'elapsed_s': record.get('elapsed_s'),
                          'adapter': report.get('adapter'), 'device': report.get('device'), 'failed_checks': report.get('failed_checks'),
                          'draws': [{k: d.get(k) for k in ('name', 'mean_r', 'mean_g', 'mean_b', 'coverage')} for d in report.get('draws', [])],
                          'sweep': report.get('sweepsummary'), 'sweep_error_programs': report.get('sweep_error_programs'),
                          'first_error': report.get('first_error'), 'moltenvk_lines': report.get('moltenvk_lines'),
                          'result': report.get('result')}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
