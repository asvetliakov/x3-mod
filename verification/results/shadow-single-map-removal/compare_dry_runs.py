#!/usr/bin/env python3
"""Single shadow map removed (2026-09-25): the launcher's dry runs before and after.

Runs `tools/manage.py launch --bottle X3 --dry-run` on the host (no Wine, nothing launched) from the
revision before the removal (BEFORE, extracted with `git archive` into a temporary directory) and from
this checkout, for the default command and for `--no-shadow-cascades`, with every X3M_* variable
removed from the shell. Checks:
  1. default: after == before minus the four removed variables (X3M_SHADOW_REPLAY_SIZE/_EXTENT/
     _DEPTH_HALF/_CAP), every other X3M_* value identical;
  2. --no-shadow-cascades: accepted (exit 0), X3M_SHADOW_CASCADES=0 with the depth replay and the apply
     still requested (the DLL applies nothing without a map), and the same before/after difference;
  3. each removed option is refused as unrecognized (exit 2);
  4. an inherited shell value of a removed variable is dropped, not forwarded.
Writes comparison.json beside this script.
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BEFORE = sys.argv[1] if len(sys.argv) > 1 else 'e17fd2ca'
REMOVED = ('X3M_SHADOW_REPLAY_SIZE', 'X3M_SHADOW_REPLAY_EXTENT', 'X3M_SHADOW_REPLAY_DEPTH_HALF', 'X3M_SHADOW_REPLAY_CAP')
REMOVED_OPTIONS = (('--shadow-replay-size', '1024'), ('--shadow-replay-extent', '250'), ('--shadow-replay-depth-half', '512'), ('--shadow-replay-cap', '512'))


def dry_run(manage, arguments, extra=None):
    environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    environ.update(extra or {})
    result = subprocess.run([sys.executable, str(manage), 'launch', '--bottle', 'X3', '--dry-run', *arguments],
                            capture_output=True, text=True, env=environ, cwd=manage.parents[1])
    if result.returncode:
        return result.returncode, result.stderr.strip().splitlines()[-1:] , None
    data = json.loads(result.stdout)
    return 0, [], {k: v for k, v in data['env'].items() if k.startswith('X3M_')}


def diff(a, b):
    return {k: [a.get(k), b.get(k)] for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)}


def main():
    with tempfile.TemporaryDirectory(prefix='x3m-single-map-before-') as tmp:
        archive = subprocess.run(['git', 'archive', BEFORE, 'tools', 'verification/probe'], cwd=ROOT, capture_output=True, check=True).stdout
        subprocess.run(['tar', '-x', '-C', tmp], input=archive, check=True)
        before_manage, after_manage = Path(tmp) / 'tools/manage.py', ROOT / 'tools/manage.py'
        report, checks = {'before': BEFORE, 'cases': {}}, 0
        for name, arguments in (('default', []), ('no-shadow-cascades', ['--no-shadow-cascades'])):
            b_code, b_err, before = dry_run(before_manage, arguments)
            a_code, a_err, after = dry_run(after_manage, arguments)
            assert b_code == 0 and a_code == 0, (name, b_code, b_err, a_code, a_err)
            d = diff(before, after)
            assert set(d) == set(REMOVED) and all(v[1] is None for v in d.values()), (name, d)
            shadow = {k: after[k] for k in sorted(after) if 'SHADOW' in k}
            report['cases'][name] = {'diff_before_after': d, 'after_shadow_variables': shadow, 'x3m_variables_after': len(after)}
            checks += 2
        after = report['cases']['no-shadow-cascades']['after_shadow_variables']
        assert (after['X3M_SHADOW_CASCADES'], after['X3M_SHADOW_REPLAY_DEPTH'], after['X3M_SHADOW_REPLAY_CANDIDATES']) == ('0', '1', '1'), after
        default = report['cases']['default']['after_shadow_variables']
        assert default['X3M_SHADOW_CASCADES'] == '250.0,1500.0,7500.0,37500.0,150000.0' and default['X3M_SHADOW_REPLAY_DEPTH'] == '1', default
        checks += 2
        refusals = {}
        for option, value in REMOVED_OPTIONS:
            code, err, _ = dry_run(after_manage, [option, value])
            assert code == 2 and err and 'unrecognized arguments' in err[0], (option, code, err)
            refusals[option] = {'exit': code, 'stderr_last_line': err[0]}
            checks += 1
        report['removed_options_refused'] = refusals
        code, err, inherited = dry_run(after_manage, [], {name: '4096' for name in REMOVED})
        assert code == 0 and not set(REMOVED) & set(inherited), (code, err, sorted(set(REMOVED) & set(inherited or {})))
        report['inherited_removed_variables_dropped'] = True
        checks += 1
        report['checks'] = checks
        (HERE / 'comparison.json').write_text(json.dumps(report, indent=1) + '\n')
        print(f'PASS checks={checks} default_diff={sorted(report["cases"]["default"]["diff_before_after"])} '
              f'no_cascades={ {k: after[k] for k in ("X3M_SHADOW_CASCADES", "X3M_SHADOW_REPLAY_DEPTH", "X3M_SUN_SHADOW_APPLY") if k in after} }')


if __name__ == '__main__':
    main()
