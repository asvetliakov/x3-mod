"""Native Windows record against the retained Wine record of run_fov_patch.py: check names and verdicts, the executed
constructor / INS_SetFocus / load results, the load sweep, the protections seen around the write, the
install / restore / confirm rows, and the timings. Prints only the comparison rows."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
wine = json.loads((ROOT / 'verification/results/bottle-X3/fov-patch.json').read_text())
native = json.loads((Path(__file__).parent / 'fov-patch.json').read_text())
for name, r in (('wine', wine), ('native', native)):
    print(name, 'passed', r['passed'], 'checks', r['check_count'], 'pass', r['pass_count'], 'exit', r['exit_status'],
          'elapsed_s', r['elapsed_s'], 'commit', r['source']['commit'][:12])
w = {c['name']: c['pass'] for c in wine['checks']}
n = {c['name']: c['pass'] for c in native['checks']}
print('same check names', list(w) == list(n), 'verdicts differ', [k for k in w if n.get(k) != w[k]])
for key in ('constructions', 'setfocus_runs', 'load_runs', 'load_sweep', 'load_arena_full', 'install_sequence', 'setfocus_override',
            'memory', 'install_rows', 'restore_rows', 'confirm_rows', 'result'):
    print(key, 'equal' if wine.get(key) == native.get(key) else ('differ', wine.get(key), native.get(key)))
pw, pn = wine['protect'], native['protect']
print('protect rows', len(pw), len(pn), 'equal' if pw == pn else [(a, b) for a, b in zip(pw, pn) if a != b][:4])
print('timing_us wine', wine['timing_us'])
print('timing_us native', native['timing_us'])
print('build_audit sections equal', wine['build_audit']['sections'] == native['build_audit']['sections'],
      'highest_other_section_end', wine['build_audit']['highest_other_section_end'], native['build_audit']['highest_other_section_end'])
