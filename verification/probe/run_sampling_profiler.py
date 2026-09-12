#!/usr/bin/env python3
"""Build and run the sampling profiler fixture under CrossOver Preview Wine.

Two runs of the same exe: X3M_PROFILE=0 (must stay silent) and X3M_PROFILE=1.
The on-run must attribute thread A to spin_a with caller_a, thread B's leaf
outside the exe with the wait_b game frame, thread C (no frame pointers) via
the scan, keep the sampler's own cost bounded, finish without deadlock and
tear down without leaking handles or threads. Results go to
verification/results/sampling-profiler.txt and -summary.json. No game launch.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess, sys, time
from game_guard import game_running  # noqa: E402
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
RESULTS = bottle.results_dir(ROOT)
BUILD = ROOT / 'verification/probe/build/sampling_profiler'
WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'
OBJDUMP = 'i686-w64-mingw32-objdump'
SOURCES = ['src/proxy/sampling_profiler.h', 'src/proxy/sampling_profiler.cpp', 'src/proxy/capture.h',
           'verification/probe/sampling_profiler_fixture.cpp', 'verification/probe/sampling_profiler_fpo.cpp',
           'verification/probe/build_sampling_profiler.sh', 'verification/probe/run_sampling_profiler.py']
OVERHEAD_BOUND = 0.35   # fraction of spinning-thread throughput lost with the sampler on


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fields(line):
    out = {}
    for token in line.split()[1:]:
        key, _, value = token.partition('=')
        out[key] = value
    return out


def number(value):
    return int(value, 0) if not any(c in value for c in '.e') or value.startswith('0x') else float(value)


def refuse_game():
    """Only real game processes count (game_guard): never a Ghidra headless run on X3AP.exe."""
    running = game_running()
    if running:
        raise SystemExit('X3AP is running; refusing to start a Wine fixture: ' + '; '.join(running))


def wait_for_idle_wine(limit_s=1800):
    """One Wine runner at a time: wait until no other probe/fixture process exists."""
    me = {os.getpid(), os.getppid()}
    deadline = time.time() + limit_s
    while True:
        busy = []
        for pattern in ('verification/probe', r'wine.*fixture'):
            out = subprocess.run(['pgrep', '-fl', pattern], capture_output=True, text=True).stdout
            for line in out.splitlines():
                pid = int(line.split()[0])
                if pid in me or 'run_sampling_profiler' in line or 'pgrep' in line:
                    continue
                busy.append(line)
        if not busy:
            return
        if time.time() > deadline:
            raise SystemExit('Another Wine fixture kept running for 30 minutes: ' + '; '.join(busy[:3]))
        time.sleep(60)


def frame_pointer_usage(exe):
    """objdump provenance: spin_a sets up an EBP frame, fpo_leaf does not."""
    text = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', str(exe)], capture_output=True, text=True, check=True).stdout
    bodies, name = {}, None
    for line in text.splitlines():
        m = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
        if m:
            name = m.group(1)
            bodies[name] = []
        elif name and line.strip():
            bodies[name].append(line.split('\t')[-1].strip())
    usage = {}
    # C++ names are mangled (__Z6spin_ay); the FPO unit exports plain C names.
    for name, pattern in (('spin_a', r'^__Z6spin_a'), ('wait_b', r'^__Z6wait_b'), ('fpo_leaf', r'^_fpo_leaf$'), ('fpo_spin', r'^_fpo_spin$')):
        symbol = next((s for s in bodies if re.match(pattern, s)), None)
        head = bodies.get(symbol, [])[:6]
        usage[name] = bool(symbol) and any(i.startswith('push') and '%ebp' in i for i in head) and any('%esp,%ebp' in i for i in head)
    return usage


def run(exe, profile, extra_env):
    refuse_game()
    wait_for_idle_wine()
    env = dict(os.environ, X3M_PROFILE=profile, X3M_PROFILE_REPORT_S='1', **extra_env)
    label = 'on' if profile == '1' else 'off'
    out_path = RESULTS / f'sampling-profiler-{label}-fixture.txt'
    err_path = RESULTS / f'sampling-profiler-{label}-wine.log'
    command = [WINE, '--bottle', bottle.BOTTLE, '--no-update', '--workdir', str(BUILD), str(exe)]
    begin = time.time()
    with out_path.open('w') as out, err_path.open('w') as err:
        try:
            proc = subprocess.run(command, env=env, stdout=out, stderr=err, timeout=120)
            code = proc.returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    return out_path, code, time.time() - begin


def parse(text):
    """Keep the fixture witnesses and the final cumulative profile tables."""
    functions, threads, seen, leaves, frames, pairs, reports, thread_lines, modules = {}, {}, {}, [], [], [], [], [], []
    profile_lines = 0
    for line in text.splitlines():
        if not line.strip():
            continue
        head = line.split(None, 1)[0]
        f = fields(line)
        if head.startswith('profile_'):
            profile_lines += 1
        if head == 'FIXTURE_FUNCTION':
            functions[f['name']] = (int(f['rva_begin'], 16), int(f['rva_end'], 16))
        elif head == 'FIXTURE_THREAD':
            threads[f['name']] = int(f['tid'])
        elif head == 'profile_thread_seen':
            seen[int(f['tid'])] = int(f['slot'])
        elif head == 'profile_report':
            reports.append({k: number(v) if k != 'scope' and k != 'table_used' else v for k, v in f.items()})
            if f.get('scope') == 'cumulative' and f.get('final') == '1':
                # Only the final cumulative block counts; earlier ones (every twelfth report) are superseded.
                leaves, frames, pairs, thread_lines = [], [], [], []
        elif head == 'profile_thread' and f.get('scope') == 'cumulative':
            thread_lines.append({k: number(v) if k != 'scope' else v for k, v in f.items()})
        elif head == 'profile_leaf' and f.get('scope') == 'cumulative':
            leaves.append((int(f['slot']), f['name'], int(f['rva'], 16), int(f['count'])))
        elif head == 'profile_frame' and f.get('scope') == 'cumulative':
            frames.append((int(f['slot']), int(f['rva'], 16), int(f['count'])))
        elif head == 'profile_pair' and f.get('scope') == 'cumulative':
            pairs.append((int(f['slot']), int(f['rva'], 16), int(f['caller'], 16), int(f['count'])))
        elif head == 'profile_module':
            modules.append(f)
    witness = {}
    for head in ('FIXTURE_BEGIN', 'FIXTURE_PROFILER', 'FIXTURE_STATUS', 'FIXTURE_END'):
        m = [fields(l) for l in text.splitlines() if l.startswith(head + ' ')]
        witness[head] = m[-1] if m else None
    # Only the last (final) cumulative block counts; it repeats every twelfth report.
    final_index = max((i for i, r in enumerate(reports) if r.get('scope') == 'cumulative' and r.get('final') == 1), default=None)
    return dict(functions=functions, threads=threads, seen=seen, leaves=leaves, frames=frames, pairs=pairs, reports=reports,
                thread_lines=thread_lines, modules=modules, witness=witness, profile_lines=profile_lines, final_index=final_index)


def inside(rva, bounds):
    return bounds[0] <= rva < bounds[1]


def evaluate(on, off, usage):
    checks, notes = {}, {}
    w = on['witness']
    end_on, end_off = w['FIXTURE_END'], off['witness']['FIXTURE_END']
    checks['on_completed'] = end_on is not None and end_on.get('exit') == '0'
    checks['off_completed'] = end_off is not None and end_off.get('exit') == '0'
    checks['off_silent'] = off['profile_lines'] == 0 and off['witness']['FIXTURE_PROFILER']['active'] == '0'
    checks['on_active'] = w['FIXTURE_PROFILER']['active'] == '1'
    checks['final_cumulative_report'] = on['final_index'] is not None
    checks['fpo_provenance'] = usage.get('spin_a') is True and usage.get('fpo_leaf') is False and usage.get('fpo_spin') is False
    fn, slots = on['functions'], {name: on['seen'].get(tid) for name, tid in on['threads'].items()}
    checks['threads_discovered'] = all(s is not None for s in slots.values())
    # The last cumulative block is the final one; earlier cumulative blocks carry lower totals.
    last = {}
    for t in on['thread_lines']:
        last[t['slot']] = t
    totals = last
    def share(slot, predicate, table):
        total = totals.get(slot, {}).get('total', 0)
        hit = sum(row[-1] for row in table if row[0] == slot and predicate(row))
        return (hit / total if total else 0.0), hit, total
    a, b, c = slots.get('a'), slots.get('b'), slots.get('c')
    if checks['threads_discovered']:
        notes['a_leaf_in_spin_a'] = share(a, lambda r: inside(r[2], fn['spin_a']), on['leaves'])
        notes['a_pair_spin_a_caller_a'] = share(a, lambda r: inside(r[1], fn['spin_a']) and inside(r[2], fn['caller_a']), on['pairs'])
        tb = totals.get(b, {})
        outside = tb.get('total', 0) - tb.get('leaf_x3ap', 0)
        notes['b_leaf_outside_exe'] = ((outside / tb['total']) if tb.get('total') else 0.0, outside, tb.get('total', 0))
        notes['b_leaf_kinds'] = {k: tb.get(k, 0) for k in ('leaf_x3ap', 'leaf_ntdll', 'leaf_wine', 'leaf_other', 'leaf_proxy')}
        notes['b_frame_in_wait_b'] = share(b, lambda r: inside(r[1], fn['wait_b']), on['frames'])
        notes['c_leaf_in_fpo_leaf'] = share(c, lambda r: inside(r[2], fn['fpo_leaf']), on['leaves'])
        notes['c_frame_in_fpo_leaf'] = share(c, lambda r: inside(r[1], fn['fpo_leaf']), on['frames'])
        notes['c_pair_fpo_leaf_fpo_spin'] = share(c, lambda r: inside(r[1], fn['fpo_leaf']) and inside(r[2], fn['fpo_spin']), on['pairs'])
        notes['c_pair_fpo_leaf_caller_c'] = share(c, lambda r: inside(r[1], fn['fpo_leaf']) and inside(r[2], fn['caller_c']), on['pairs'])
        checks['a_leaf_attribution'] = notes['a_leaf_in_spin_a'][0] >= 0.9
        checks['a_caller_pair'] = notes['a_pair_spin_a_caller_a'][0] >= 0.9
        checks['b_leaf_outside_exe'] = notes['b_leaf_outside_exe'][0] >= 0.9
        checks['b_frame_wait_b'] = notes['b_frame_in_wait_b'][0] >= 0.9
        checks['c_leaf_attribution'] = notes['c_leaf_in_fpo_leaf'][0] >= 0.9
        checks['c_scan_caller_pair'] = notes['c_pair_fpo_leaf_fpo_spin'][0] >= 0.5
        checks['a_samples_present'] = totals.get(a, {}).get('total', 0) >= 100
    status = w['FIXTURE_STATUS']
    notes['sampler_cost'] = {k: number(status[k]) for k in ('ticks', 'samples', 'dropped', 'reports', 'threads', 'modules', 'tick_us_mean', 'tick_us_max', 'report_us', 'refresh_us')}
    checks['tick_cost_reported'] = notes['sampler_cost']['ticks'] > 0 and notes['sampler_cost']['tick_us_mean'] > 0
    checks['no_drops'] = notes['sampler_cost']['dropped'] == 0
    checks['stop_bounded'] = int(end_on['stop_us']) < 5_000_000
    ratio = {}
    for key in ('iterations_a', 'iterations_c', 'main_iterations'):
        base, with_profile = int(end_off[key]), int(end_on[key])
        ratio[key] = dict(off=base, on=with_profile, overhead=(1 - with_profile / base) if base else None)
    notes['overhead'] = ratio
    notes['elapsed_ms'] = dict(off=int(end_off['elapsed_ms']), on=int(end_on['elapsed_ms']))
    checks['overhead_within_bound'] = all(r['overhead'] is not None and r['overhead'] <= OVERHEAD_BOUND for k, r in ratio.items() if k != 'main_iterations')
    checks['handles_released'] = int(end_on['handles_after']) <= int(end_on['handles_before'])
    checks['threads_released'] = int(end_on['threads_after']) == int(end_on['threads_before']) and end_on['active_after'] == '0'
    checks['wait_completed'] = end_on['wait_result'] == '1'
    checks['module_table_recorded'] = any(m.get('kind') == 'x3ap' for m in on['modules']) and any(m.get('kind') == 'ntdll' for m in on['modules'])
    return checks, notes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interval-us', default='2000')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    report = {'passed': False, 'bottle': bottle.describe(), 'phase': 'building', 'game_launched': False}
    summary = RESULTS / 'sampling-profiler-summary.json'
    try:
        refuse_game()
        report['sources'] = {s: sha(ROOT / s) for s in SOURCES}
        if not args.skip_build:
            subprocess.run(['sh', str(ROOT / 'verification/probe/build_sampling_profiler.sh')], cwd=ROOT, check=True)
        exe = BUILD / 'sampling_profiler_fixture.exe'
        report['executable_sha256'] = sha(exe)
        usage = frame_pointer_usage(exe)
        report['frame_pointer_usage'] = usage
        report['phase'] = 'running'
        extra = {'X3M_PROFILE_INTERVAL_US': args.interval_us}
        runs = {}
        for profile in ('0', '1'):
            path, code, seconds = run(exe, profile, extra)
            runs[profile] = dict(output=str(path.relative_to(ROOT)), exit_code=code, wall_s=round(seconds, 3), output_sha256=sha(path))
        report['runs'] = runs
        on = parse((RESULTS / 'sampling-profiler-on-fixture.txt').read_text(errors='replace'))
        off = parse((RESULTS / 'sampling-profiler-off-fixture.txt').read_text(errors='replace'))
        checks, notes = evaluate(on, off, usage)
        report['checks'] = checks
        report['notes'] = notes
        report['functions'] = {k: [hex(a), hex(b)] for k, (a, b) in on['functions'].items()}
        report['slots'] = {name: on['seen'].get(tid) for name, tid in on['threads'].items()}
        report['sources_unchanged'] = report['sources'] == {s: sha(ROOT / s) for s in SOURCES}
        report['passed'] = all(checks.values()) and report['sources_unchanged'] and runs['0']['exit_code'] == 0 and runs['1']['exit_code'] == 0
        report['phase'] = 'complete'
        lines = [f'sampling profiler fixture (Wine, CrossOver Preview {bottle.label()}), no game launch',
                 f"interval_us={args.interval_us} report_s=1 passed={report['passed']}", '']
        for key, value in checks.items():
            lines.append(f'check {key}={int(bool(value))}')
        lines.append('')
        for key, value in notes.items():
            lines.append(f'{key}={json.dumps(value)}')
        (RESULTS / 'sampling-profiler.txt').write_text('\n'.join(lines) + '\n')
    except (Exception, KeyboardInterrupt) as error:
        report.update(passed=False, phase='failed', error=repr(error))
    finally:
        summary.write_text(json.dumps(report, indent=2, default=str) + '\n')
        print(json.dumps(report, indent=2, default=str))
    raise SystemExit(0 if report['passed'] else 1)


if __name__ == '__main__':
    main()
