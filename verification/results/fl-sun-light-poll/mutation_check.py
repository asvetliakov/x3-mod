#!/usr/bin/env python3
"""Mutation check of the FL sun light poll fixture (docs/verification/farnhams-legacy.md, "Step 7").

Applies one source mutation at a time to src/engine/fl/sun_light_poll_fl.cpp, rebuilds and runs the native fixture
through the host test's build_and_run (verification/analysis/test_fl_sun_light_poll.py) and records the fixture's
result and the FAIL labels per mutation; the source is restored byte for byte afterwards (also on error). Each
mutation must fail; the unmutated run must pass. WSL2 with interop only. Writes mutation_check.json beside this
script and prints it.

usage: python3 verification/results/fl-sun-light-poll/mutation_check.py
"""
import hashlib
import importlib.util
import json
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / 'src/engine/fl/sun_light_poll_fl.cpp'
MUTATIONS = {
    'round_not_truncate': [('const std::int32_t luma = std::int32_t(sum);',
                            'const std::int32_t luma = std::int32_t(sum + 0.5);')],
    'node_read_0xc8': [('node_block_size = node_record + 4 - node_block;', 'node_block_size = node_record + 8 - node_block;'),
                       ('static_assert(node_block_size == 0xc4,', 'static_assert(node_block_size == 0xc8,')],
    'light_bit_removed': [('if (!(flags & light_bit)) return finish(Status::Layout);',
                           'if (false) return finish(Status::Layout);')],
    'unsigned_range_compare': [('std::int32_t(word(block, node_range)) > range_threshold',
                                'word(block, node_range) > std::uint32_t(range_threshold)')],
    'tie_rule_removed': [('score > best.score || (score == best.score && reach > best.reach)', 'score > best.score')],
}
spec = importlib.util.spec_from_file_location('test_fl_sun_light_poll',
                                              ROOT / 'verification/analysis/test_fl_sun_light_poll.py')
test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(test)


def run():
    with tempfile.TemporaryDirectory(prefix='x3-fl-sun-mut-') as tmp:
        result = test.build_and_run(tmp)
    if result is None:
        return None
    m = re.search(r'^RESULT (\w+) checks=(\d+) failures=(\d+)', result.stdout, re.M)
    return {'result': m.group(1) if m else None, 'checks': int(m.group(2)) if m else None,
            'failures': int(m.group(3)) if m else None,
            'failed': sorted(set(re.findall(r'^FAIL (.*)$', result.stdout, re.M)))}


def main():
    original = SOURCE.read_bytes()
    text = original.decode()
    out = {'source_sha256': hashlib.sha256(original).hexdigest()}
    try:
        for name, edits in MUTATIONS.items():
            mutated = text
            for old, new in edits:
                if mutated.count(old) != 1:
                    raise SystemExit(f'{name}: pattern not found exactly once')
                mutated = mutated.replace(old, new)
            SOURCE.write_text(mutated)
            out[name] = run()
            SOURCE.write_bytes(original)
        out['unmutated'] = run()
    finally:
        SOURCE.write_bytes(original)
    out['restored'] = hashlib.sha256(SOURCE.read_bytes()).hexdigest() == out['source_sha256']
    (HERE / 'mutation_check.json').write_text(json.dumps(out, indent=1) + '\n')
    print(json.dumps(out, indent=1))
    ok = out['restored'] and all(out[k] and out[k]['result'] == 'FAIL' for k in MUTATIONS) and \
        out['unmutated'] and out['unmutated']['result'] == 'PASS'
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
