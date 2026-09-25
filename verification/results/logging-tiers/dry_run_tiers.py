#!/usr/bin/env python3
"""Logging tiers (docs/architecture/logging-tiers.md, 2026-09-26): the launcher's dry runs before and after.

Runs `tools/manage.py launch --bottle X3 --dry-run` on the host (no Wine, nothing launched) for the default launch,
--debug, --perf, --debug --perf and --vanilla, and the same default and --vanilla launches of the launcher at BASE
(9a668e81, extracted with git show into a temporary directory). Prints the X3M_* variable counts and asserts:
- the default launch sends no X3M_DEBUG / X3M_PERF and none of the launcher's TIERED_VARIABLES;
- --debug / --perf add exactly X3M_DEBUG=1 / X3M_PERF=1 to the default launch;
- against BASE, every variable that changed (default and vanilla) is a logging variable (TIERED_VARIABLES), i.e. no
  functional variable changed;
- an inherited value of every tiered variable is dropped.
Writes dry-runs.json beside this script.

    python3 verification/results/logging-tiers/dry_run_tiers.py
"""
import io
import json
import os
import tarfile
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = '9a668e81'
FORMS = {'default': [], 'debug': ['--debug'], 'perf': ['--perf'], 'debug_perf': ['--debug', '--perf'], 'vanilla': ['--vanilla']}


def tiered():
    sys.path.insert(0, str(ROOT / 'tools'))
    import importlib.util
    spec = importlib.util.spec_from_file_location('dry_run_tiers_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return set(module.TIERED_VARIABLES)


def dry_run(script, arguments, inherited=None):
    environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    environ.update(inherited or {})
    result = subprocess.run([sys.executable, str(script), 'launch', '--bottle', 'X3', '--dry-run', *arguments],
                            capture_output=True, text=True, env=environ, cwd=ROOT)
    if result.returncode:
        raise SystemExit(f'dry run {script.name} {arguments} failed: {result.stderr[-400:]}')
    data = json.loads(result.stdout)
    return {k: v for k, v in data['env'].items() if k.startswith('X3M_')}


def diff(a, b):
    return {k: [a.get(k), b.get(k)] for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)}


def main():
    names = tiered()
    new = {form: dry_run(ROOT / 'tools/manage.py', args) for form, args in FORMS.items()}
    with tempfile.TemporaryDirectory() as directory:
        # The whole tools/ tree at BASE (the launcher imports its siblings), run with this checkout as the working directory.
        archive = subprocess.run(['git', 'archive', '--format=tar', BASE, 'tools', 'verification/probe/exe_identity.py'], cwd=ROOT, capture_output=True, check=True).stdout
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(directory)
        old = {form: dry_run(Path(directory) / 'tools/manage.py', FORMS[form]) for form in ('default', 'vanilla')}
    inherited = dry_run(ROOT / 'tools/manage.py', [], {name: '1' for name in names})
    result = {
        'counts': {**{f'new_{k}': len(v) for k, v in new.items()}, **{f'base_{k}': len(v) for k, v in old.items()}},
        'default_vs_base': diff(old['default'], new['default']),
        'vanilla_vs_base': diff(old['vanilla'], new['vanilla']),
        'debug_vs_default': diff(new['default'], new['debug']),
        'perf_vs_default': diff(new['default'], new['perf']),
        'debug_perf_vs_default': diff(new['default'], new['debug_perf']),
        'inherited_tiered_vs_default': diff(new['default'], inherited),
    }
    checks = {
        'default launch: no X3M_DEBUG/X3M_PERF, no tiered variable': not (set(new['default']) & names),
        'vanilla launch: no tiered variable': not (set(new['vanilla']) & names),
        '--debug adds exactly X3M_DEBUG=1': result['debug_vs_default'] == {'X3M_DEBUG': [None, '1']},
        '--perf adds exactly X3M_PERF=1': result['perf_vs_default'] == {'X3M_PERF': [None, '1']},
        '--debug --perf adds exactly the two groups': result['debug_perf_vs_default'] == {'X3M_DEBUG': [None, '1'], 'X3M_PERF': [None, '1']},
        'no functional variable changed against the base (default)': set(result['default_vs_base']) <= names
            and all(b is None for a, b in result['default_vs_base'].values()),
        'no functional variable changed against the base (vanilla)': set(result['vanilla_vs_base']) <= names
            and all(b is None for a, b in result['vanilla_vs_base'].values()),
        'inherited tiered values dropped': result['inherited_tiered_vs_default'] == {},
    }
    result['checks'] = checks
    (HERE / 'dry-runs.json').write_text(json.dumps(result, indent=1, sort_keys=True) + '\n')
    print('counts', result['counts'])
    for key in ('default_vs_base', 'vanilla_vs_base'):
        print(f'{key}: {len(result[key])} variables, all logging variables no longer sent: {sorted(result[key])}')
    for name, ok in checks.items():
        print('PASS' if ok else 'FAIL', name)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
