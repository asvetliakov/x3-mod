"""Refresh only the tool_sources digests of the nine bloom provenance records (no compilation, no Wine).

Usage (repo root): PYTHONPATH=verification/probe python3 verification/results/bloom-tool-source-refresh/refresh_tool_sources.py [--check]
Refuses unless each record round-trips byte-identically through json.dumps(indent=2); every other field is untouched.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools/shaders'), str(ROOT / 'verification/probe')]
import generate_bloom_programs as tool  # noqa: E402

check = '--check' in sys.argv[1:]
changed = 0
for name, config in tool.SHADERS.items():
    path = config['provenance']
    raw = path.read_text()
    record = json.loads(raw)
    if json.dumps(record, indent=2) + '\n' != raw:
        raise SystemExit(f'{path}: does not round-trip; refusing to rewrite')
    fresh = {p: tool.digest(ROOT / p) for p in record['tool_sources']}
    stale = sorted(p for p in fresh if fresh[p] != record['tool_sources'][p])
    print(f'{name}: {len(stale)} stale tool digest(s) {stale}')
    if stale and not check:
        record['tool_sources'] = fresh
        path.write_text(json.dumps(record, indent=2) + '\n')
        changed += 1
print(f'records rewritten: {changed}')
