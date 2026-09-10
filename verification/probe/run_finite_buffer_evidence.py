#!/usr/bin/env python3
"""Fresh original portable metadata fixtures; no Wine, GPU, COM, or game input."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[2]
results = root / 'verification/results'
build = root / 'verification/probe/build'
paths = [root / name for name in (
    'src/ownership/finite_buffer_evidence.h', 'src/ownership/finite_buffer_evidence.cpp',
    'verification/probe/finite_buffer_evidence.cpp',
    'verification/probe/build_finite_buffer_evidence.sh',
    'verification/probe/run_finite_buffer_evidence.py',
    'verification/analysis/test_finite_evidence_runner.py')]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def hashes():
    return {str(path.relative_to(root)): sha(path) for path in paths}

def terminal_result(text):
    lines = text.splitlines()
    terminals = [line for line in lines if line.startswith('RESULT ')]
    if len(terminals) != 1 or not lines or lines[-1] != terminals[0]:
        return None
    return re.fullmatch(r'RESULT PASS checks=(\d+) all_atlases_released=1', terminals[0])

report = {'passed': False,
          'game_launched': False, 'gpu_used': False,
          'scope': 'Portable integer-only finite-position atlas/index certificate; owner native mapping/budget/lifetime gates remain separate',
          'i686_cpu_baseline': 'SSE2; stack realignment; four-byte incoming Win32 stack'}
summary = results / 'finite-buffer-evidence-summary.json'
# Invalidate old success before any input hashing/build operation can fail.
summary.write_text(json.dumps(report, indent=2) + '\n')
try:
    report['sources_before_build'] = hashes()
    subprocess.run(['sh', str(root / 'verification/probe/build_finite_buffer_evidence.sh')], check=True, cwd=root, timeout=60)
    assert hashes() == report['sources_before_build'], 'Source changed during build'
    artifacts = [build / name for name in ('finite_buffer_evidence', 'finite_buffer_evidence_sanitized', 'finite_buffer_evidence_i686.o')]
    report['artifacts'] = {p.name: sha(p) for p in artifacts}
    report['executions'] = []
    for path in artifacts[:2]:
        run = subprocess.run([str(path)], capture_output=True, text=True, timeout=60,
                             env=dict(os.environ, ASAN_OPTIONS='halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
        name = 'finite-buffer-evidence' + ('-sanitized' if path.name.endswith('_sanitized') else '')
        log = results / (name + '.txt')
        log.write_text(run.stdout + run.stderr)
        result = terminal_result(run.stdout)
        assert run.returncode == 0 and result and 'FAIL' not in run.stdout and not run.stderr, log.read_text()
        assert int(result[1]) == 214651
        assert 'HALF encodings_per_lane=65536 lanes=3 finite=190464 nonfinite=6144' in run.stdout
        assert 'FLOAT payloads_per_lane=2560 lanes=3 finite=7650 nonfinite=30' in run.stdout
        timing = re.search(r'CACHE vertices=4096 cold_components=12288 warm_hits=10000 cold_ns=(\d+) warm_total_ns=(\d+) classified_bytes=49152', run.stdout)
        assert timing
        report['executions'].append({'artifact': path.name, 'exit_code': run.returncode,
            'checks': int(result[1]), 'report_sha256': sha(log),
            'cold_query_nanoseconds': int(timing[1]), 'warm_10000_queries_nanoseconds': int(timing[2])})
    disassembly = subprocess.run(['i686-w64-mingw32-objdump', '-d', str(artifacts[2])], check=True, capture_output=True, text=True, timeout=15).stdout
    instructions = re.findall(r'^\s*[\da-f]+:\s+(?:(?:[\da-f]{2})\s+)+([a-z][a-z\d]*)', disassembly, re.M)
    assert instructions
    floating = sorted({op for op in instructions if op.startswith('f') or re.fullmatch(r'(?:add|sub|mul|div|min|max|sqrt|rsqrt|rcp|hadd|hsub|round)(?:ss|sd|ps|pd)', op) or op.startswith(('cvt', 'comis', 'ucomis'))})
    report['i686_floating_arithmetic_instructions'] = floating
    assert not floating, floating
    report['source_unchanged'] = hashes() == report['sources_before_build']
    report['artifacts_unchanged'] = all(sha(p) == report['artifacts'][p.name] for p in artifacts)
    assert report['source_unchanged'] and report['artifacts_unchanged'], 'Provenance changed'
    report['passed'] = True
finally:
    summary.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
