#!/usr/bin/env python3
"""Fresh-build ABI controls and compare the production naked thunk's code."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / 'verification/probe/build/sse2-abi'
RESULTS = ROOT / 'verification/results'
BUILD.mkdir(parents=True, exist_ok=True)
RESULTS.mkdir(exist_ok=True)
INPUTS = ['verification/probe/sse2_abi.cpp', 'verification/probe/run_sse2_abi.py',
          'src/proxy/object_trace.cpp', 'src/proxy/object_trace.h']
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def hashes():
    return {name: sha(ROOT / name) for name in INPUTS}
def run(command):
    return subprocess.run(command, check=True, capture_output=True, text=True, timeout=45).stdout
def instructions(disassembly, symbol):
    match = re.search(r'^\w+ <' + re.escape(symbol) + r'>:\n(.*?)(?=^\w+ <|\Z)', disassembly, re.M | re.S)
    if not match:
        raise RuntimeError('missing disassembly symbol: ' + symbol)
    # Strip addresses/bytes. Object-file relocation targets are unchanged here.
    return [line.split('\t')[-1].strip() for line in match[1].splitlines() if '\t' in line]

before = hashes()
base = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-msse2', '-mfpmath=sse']
variants = {'default': [], 'realign': ['-mstackrealign'],
            'explicit_legacy': ['-mstackrealign', '-mincoming-stack-boundary=2'],
            'assumed16_realign': ['-mincoming-stack-boundary=4', '-mstackrealign'],
            'assumed16_negative': ['-mno-stackrealign', '-mincoming-stack-boundary=4']}
report = dict(started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              compiler=run([base[0], '--version']).splitlines()[0], sources_before=before,
              scope='Original console probe only; no game, installation or bottle setting changes.', variants={})
log = []
for name, flags in variants.items():
    exe = BUILD / (name + '.exe')
    command = base + flags + ['-static', str(ROOT / INPUTS[0]), '-o', str(exe)]
    run(command)
    assert hashes() == before, 'source changed during build'
    exe_hash = sha(exe)
    dump = run(['i686-w64-mingw32-objdump', '-d', str(exe)])
    selected = {symbol: instructions(dump, symbol) for symbol in
                ['_std_callback@8', '_this_callback', '_c_callback', '_scalar_return', '_legacy_call']}
    launch = ['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine',
              '--bottle', 'Steam', '--no-update', '--workdir', str(BUILD), str(exe)]
    negative = name.startswith('assumed16_')
    if negative:
        launch.append('negative')
    result = subprocess.run(launch, capture_output=True, text=True, env=dict(os.environ), timeout=45)
    expected = ((result.returncode == 73 and 'EXPECTED_NEGATIVE_CONTROL_MISALIGNED_MOVAPS' in result.stdout) or
                (result.returncode == 1 and 'CASE abi=stdcall pre_call_mod16=4 local_mod16=4 errors=0 result=FAIL' in result.stdout)) if negative else (
        result.returncode == 0 and result.stdout.count('result=PASS') == 13 and 'RESULT checks=12 status=PASS' in result.stdout)
    data = dict(build_command=command, command=launch, executable_sha256=exe_hash,
                exit_code=result.returncode, expected_result=expected,
                source_and_executable_unchanged=hashes() == before and sha(exe) == exe_hash,
                disassembly=selected)
    scalar = '\n'.join(selected['_scalar_return'])
    data['scalar_sse_arithmetic_and_st0_return'] = 'mulss' in scalar and 'addss' in scalar and 'flds' in scalar and not re.search(r'\bf(?:add|mul|div|sub)', scalar)
    report['variants'][name] = data
    log.append('VARIANT ' + name + '\n' + result.stdout)
    (RESULTS / ('sse2-abi-' + name + '-wine.log')).write_text(result.stderr)

# Compile production object-trace without linking or altering the live build.
# naked dispatch must retain exactly its handwritten frame/SEH instructions.
thunks = {}
for name in ['default', 'realign', 'explicit_legacy']:
    obj = BUILD / ('object_trace-' + name + '.o')
    command = base + variants[name] + ['-c', str(ROOT / 'src/proxy/object_trace.cpp'), '-o', str(obj)]
    run(command)
    thunks[name] = instructions(run(['i686-w64-mingw32-objdump', '-d', str(obj)]), '_x3m_object_dispatch')
report['object_dispatch_instructions_identical'] = thunks['default'] == thunks['realign'] == thunks['explicit_legacy']
report['object_dispatch_instructions'] = thunks['realign']
report['sources_after'] = hashes()
report['passed'] = all(v['expected_result'] and v['source_and_executable_unchanged'] and v['scalar_sse_arithmetic_and_st0_return'] for v in report['variants'].values()) and report['object_dispatch_instructions_identical'] and hashes() == before
(RESULTS / 'sse2-abi.txt').write_text('\n'.join(log))
report['report_sha256'] = sha(RESULTS / 'sse2-abi.txt')
(RESULTS / 'sse2-abi-summary.json').write_text(json.dumps(report, indent=2) + '\n')
print('\n'.join(log))
print('NAKED_THUNK_IDENTICAL', report['object_dispatch_instructions_identical'])
print('PASSED', report['passed'])
raise SystemExit(0 if report['passed'] else 1)
