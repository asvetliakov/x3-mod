#!/usr/bin/env python3
"""Compare the production transformer with independently derived local variants."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
capture = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures'
build = root / 'verification/probe/build/material-radiance-profiles'
build.mkdir(parents=True, exist_ok=True)
inputs = ['src/renderer/material_radiance.cpp', 'src/renderer/material_radiance.h',
          'src/renderer/material_radiance_profiles_inc.h',
          'verification/probe/material_radiance_profiles.cpp',
          'verification/probe/run_material_radiance_profiles.py',
          'docs/reverse-engineering/material-radiance-profiles.json']
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
hashes = lambda: {name: sha(root / name) for name in inputs}
before = hashes()
executable = build / 'transform'
subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                str(root / inputs[0]), str(root / inputs[3]), '-o', str(executable)], check=True)
assert hashes() == before, 'Inputs changed during compilation'
exe_hash = sha(executable)
profiles = json.loads((root / inputs[-1]).read_text())['profiles']
report = {'sources': before, 'executable_sha256': exe_hash, 'fresh_build': True,
          'game_launched': False, 'gpu_rendered': False, 'cases': []}
for profile in profiles:
    source = capture / ('ps_' + profile['fnv1a64'] + '.bin')
    assert sha(source) == profile['sha256'], 'Local captured shader does not match inspected source'
    output = build / source.name
    subprocess.run([str(executable), str(source), str(output)], check=True)
    actual = sha(output)
    assert actual == profile['patched_sha256'], 'Production bytes differ from independent transformation'
    assert output.stat().st_size == profile['patched_byte_count']
    assert sha(source) == profile['sha256'], 'Original shader changed'
    report['cases'].append({'original_fnv': profile['fnv1a64'], 'original_sha256': sha(source),
                            'variant_sha256': actual, 'sites': len(profile['sites']), 'passed': True})
assert hashes() == before and sha(executable) == exe_hash, 'Inputs changed during execution'
report.update(passed=len(report['cases']) == 5, source_and_executable_unchanged=True)
(root / 'verification/results/material-radiance-profiles-summary.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
