"""Run134 candidate (copy of the Run130 helper, unchanged logic): do the eight engine fixture records bind to a tree?

Usage (from the tree root): python3 <this> [commit]
For each record: its recorded source hashes compared to the tree, and the
record's closure (the i686 g++ -MM -MG include closure of the translation
units its runner/builder compiles, with that runner's FLAGS, plus the runner
and builder) intersected with git diff <base> <commit>, where the bases are
the record's source commit (without -dirty) and the commit that last
committed the record file (so a dirty fixture input the record ran with is
covered). seam-engine-light runs the whole production DLL, so its closure is
every path under src/ plus the motion-output fixture inputs. Hashed files are
bound by their hash. A record binds when both lists are empty and it passed.
"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path.cwd()
sys.path.insert(0, str(ROOT / 'verification/probe'))
import run_engine_plumes, run_engine_shimmer, run_engine_effects, run_engine_light, run_chase_aim_trace  # noqa: E402
from build_cull_census import NO_SSE, FIXTURE_DEFINE  # noqa: E402

R = 'verification/results/'
P = 'verification/probe/'
RECORDS = {
    'engine-plumes': (R + 'bottle-X3/engine-plumes/summary.json', run_engine_plumes.FLAGS,
                      [(s, []) for s in run_engine_plumes.SOURCES], [P + 'run_engine_plumes.py', P + 'png_writer.py']),
    'engine-ribbons': (R + 'bottle-X3/engine-ribbons/summary.json', run_engine_plumes.FLAGS,
                       [(s, []) for s in ('verification/probe/engine_ribbons_fixture.cpp', 'src/renderer/engine_ribbons_pass.cpp',
                                          'src/renderer/engine_plumes_pass.cpp', 'src/renderer/temporal_pass.cpp')],
                       [P + 'run_engine_ribbons.py', P + 'run_engine_plumes.py']),
    'engine-shimmer': (R + 'bottle-X3/engine-shimmer/summary.json', run_engine_shimmer.FLAGS,
                       [(s, []) for s in run_engine_shimmer.SOURCES], [P + 'run_engine_shimmer.py']),
    'engine-effects': (R + 'bottle-X3/engine-effects/summary.json', run_engine_effects.FLAGS,
                       [(P + 'engine_effects_fixture.cpp', [])], [P + 'run_engine_effects.py']),
    'engine-light-gpu': (R + 'bottle-X3/engine-light-gpu.json', run_engine_light.FLAGS,
                         [(s, []) for s in (P + 'engine_light_fixture.cpp', 'src/renderer/linear_material.cpp',
                                            'src/renderer/material_motion.cpp')], [P + 'run_engine_light.py']),
    'engine-effects-patch': (R + 'bottle-X3/engine-effects-patch.json', run_chase_aim_trace.FLAGS,
                             [(P + 'engine_effects_patch_fixture.cpp', []),
                              ('src/proxy/engine_effects_patch.cpp', ['-include', P + 'engine_effects_patch_fixture_shim.h',
                                                                      '-DX3M_ENGINE_EFFECTS_SHIM']),
                              ('src/proxy/engine_patch.cpp', [])],
                             [P + 'build_engine_effects_patch.py', P + 'run_engine_effects_patch.py']),
    'cull-small-parts-cpu': (R + 'cull-small-parts-cpu.json', run_chase_aim_trace.FLAGS,
                             [(P + 'cull_small_parts_fixture.cpp', [FIXTURE_DEFINE]), ('src/proxy/cull_small_parts.cpp', []),
                              ('src/proxy/lens_flare_cull.cpp', [FIXTURE_DEFINE]), ('src/proxy/cull_census.cpp', NO_SSE),
                              ('src/proxy/engine_far_jets.cpp', [*NO_SSE, '-fno-exceptions']), ('src/proxy/engine_patch.cpp', []),
                              ('src/proxy/engine_memory.cpp', NO_SSE)],
                             [P + 'build_cull_small_parts.py', P + 'run_cull_small_parts.py', P + 'build_cull_census.py',
                              'verification/fixtures/run131-cull-census-rows.json']),
    'seam-engine-light': (R + 'bottle-X3/seam-engine-light-fixture.json', None, [],
                          [P + 'motion_output_fixture.cpp', P + 'motion_output_engine_light_seam_inc.h', P + 'run_motion_output.py']),
}


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


head = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
out = {}
for name, (path, flags, units, extra) in RECORDS.items():
    rec = json.loads((ROOT / path).read_text())
    src = rec.get('source') if isinstance(rec.get('source'), dict) else {}
    hashes = src.get('sha256') or rec.get('sources_sha256') or {}
    mismatch = [p for p, h in hashes.items() if hashlib.sha256((ROOT / p).read_bytes()).hexdigest() != h]
    closure = set(extra) | set(hashes)
    for unit, more in units:
        mm = subprocess.run(['i686-w64-mingw32-g++', *flags, *more, '-MM', '-MG', unit], cwd=ROOT, check=True,
                            capture_output=True, text=True).stdout
        for tok in mm.replace('\\\n', ' ').split()[1:]:
            p = Path(tok)
            p = (ROOT / p).resolve() if not p.is_absolute() else p.resolve()
            try:
                closure.add(str(p.relative_to(ROOT.resolve())))
            except ValueError:
                pass  # toolchain headers
    bases = [b for b in (src.get('commit', '').replace('-dirty', ''), git('log', '-1', '--format=%H', '--', path).strip()) if b]
    changed = set()
    for b in bases:
        changed |= set(git('diff', '--name-only', b, head).split())
    # a hashed file equal to its recorded hash is bound by the hash, whatever the diff says
    hit = sorted(p for p in changed if p not in hashes and (p in closure or (units == [] and p.startswith('src/'))))
    passed = rec.get('passed', rec.get('exit_status') == 0 and rec.get('failures') == 0 if 'failures' in rec else rec.get('exit') == 0)
    out[name] = {'record_commit': src.get('commit'), 'bases': [b[:8] for b in bases], 'hashes': len(hashes),
                 'hash_mismatch': mismatch, 'closure_files': len(closure) if units else 'src/**', 'closure_changed': hit,
                 'passed': passed, 'binds': bool(passed) and not mismatch and not hit}
print(json.dumps(out, indent=1))
