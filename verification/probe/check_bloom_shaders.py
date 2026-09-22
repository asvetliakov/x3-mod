#!/usr/bin/env python3
"""Compile authored bloom kernels as ps_3_0; retain bytes locally, not in the DLL.

Run through wine_lock.py. This creates no D3D device and does not establish GPU
execution, filtering precision, caps admission or renderer integration. The
shared generator supplies include expansion, compiler invocation and bytecode
framing validation; this runner binds its own configuration as provenance too.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import re
import struct

import bottle
from bloom_shader_limits import check_bytecode, PROFILE_SOURCE, INDEX_SOURCE

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / 'tools/shaders/generate_rigid_motion_pixel.py'
spec = importlib.util.spec_from_file_location('bloom_shader_generator', GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
KERNELS = ('extract_gamma', 'extract_srgb', 'extract_none',
           'extract_even_gamma', 'extract_even_srgb', 'extract_even_none', 'down', 'up')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--d3dx', type=Path, default=bottle.game_dir('X3') / 'd3dx9_37.dll')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--summary', type=Path, default=ROOT / 'verification/results/bloom-filter-compile.json')
    args = parser.parse_args()
    if generator.game_running():
        raise RuntimeError('X3AP running or process inventory failed; postpone compilation')
    output = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='x3-bloom-kernels-'))
    output.mkdir(parents=True, exist_ok=True)
    inputs = {Path(__file__).resolve(), Path(__file__).with_name('bloom_shader_limits.py').resolve(),
              PROFILE_SOURCE, INDEX_SOURCE, Path(__file__).with_name('game_guard.py').resolve(),
              GENERATOR, generator.COMPILER_SOURCE, args.d3dx.resolve()}
    for name in KERNELS:
        source = ROOT / f'src/temporal/bloom_{name}_ps.hlsl'
        _, included = generator.expand_includes(source)
        inputs.update([source, *included])
        generator.SHADERS[f'bloom_{name}'] = dict(source=source,
            header=output / f'bloom_{name}_program_inc.h',
            provenance=output / f'bloom_{name}_program.json')
    before = {str(path): digest(path) for path in sorted(inputs)}
    records = []
    report = dict(schema=1, passed=False, phase='running',
        scope='Native ps_3_0 compilation, framing and conservative static budgets',
        game_launched=False, creates_d3d_device=False, gpu_execution_verified=False,
        integrated_into_renderer=False, installed_dll_changed=False, bottle='Steam',
        inputs_before=before, retained_dir=str(output), kernels=records)
    args.summary.parent.mkdir(parents=True, exist_ok=True)
    if args.summary.exists():
        (output / 'prior-summary.json').write_bytes(args.summary.read_bytes())
    args.summary.write_text(json.dumps(report, indent=2) + '\n')
    args.check = False
    try:
        for name in KERNELS:
            report['current_kernel'] = name
            item = generator.compile_one(f'bloom_{name}', args)
            path = output / f'bloom_{name}_program.json'
            header_path = output / f'bloom_{name}_program_inc.h'
            native_record = json.loads(path.read_text())
            header = header_path.read_text()
            old_comment = '// Reproduce: python3 tools/shaders/generate_rigid_motion_pixel.py --check'
            new_comment = '// Reproduce: python3 verification/probe/wine_lock.py python3 verification/probe/check_bloom_shaders.py'
            if header.count(old_comment) != 1:
                raise RuntimeError('Unexpected shared-generator header annotation')
            header = header.replace(old_comment, new_comment)
            header_path.write_text(header)
            native_record['header_sha256'] = digest(header_path)
            native_record['header_annotation_tool'] = str(Path(__file__).resolve().relative_to(ROOT))
            native_record['tool_sources'][native_record['header_annotation_tool']] = before[str(Path(__file__).resolve())]
            path.write_text(json.dumps(native_record, indent=2) + '\n')
            item.update(provenance=native_record,
                        retained_manifest=str(path), retained_manifest_sha256=digest(path),
                        retained_header=str(header_path))
            words = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{8})u', header)]
            code = struct.pack('<' + 'I' * len(words), *words)
            if len(words) != item['word_count'] or hashlib.sha256(code).hexdigest() != item['bytecode_sha256']:
                raise RuntimeError('Retained header differs from native compiled bytecode')
            records.append(item)
            item['sm3_budget'] = check_bytecode(code)
        after = {path: digest(path) for path in before}
        report['inputs_after'] = after
        report['inputs_stable'] = before == after
        if before != after:
            raise RuntimeError('Bloom compilation inputs changed')
    except Exception as error:
        after = {path: digest(path) if Path(path).is_file() else None for path in before}
        report.update(phase='failed', error=str(error), inputs_after=after, inputs_stable=before == after)
        args.summary.write_text(json.dumps(report, indent=2) + '\n')
        raise
    report.pop('current_kernel', None)
    report.update(passed=True, phase='complete')
    args.summary.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(passed=True, kernels=len(records), summary=str(args.summary), retained=str(output))))


if __name__ == '__main__':
    main()
