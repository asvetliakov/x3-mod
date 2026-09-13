#!/usr/bin/env python3
"""Native compile/budget provenance for bloom display candidate; no renderer.

Run through wine_lock.py. Default compiles only the selected candidate, leaving
all existing embedded programs untouched. --compare-fused also attempts the
diagnostic five-AgX alternative; a compilation/budget failure returns nonzero
and remains explicitly recorded, never interpreted as GPU acceptance.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import tempfile

from bloom_shader_limits import check_bytecode, PROFILE_SOURCE, INDEX_SOURCE

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / 'tools/shaders/generate_rigid_motion_pixel.py'
spec = importlib.util.spec_from_file_location('composition_shader_generator', GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
REPRODUCE = ('python3 verification/probe/wine_lock.py python3 '
             'verification/probe/check_bloom_composition.py')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def annotate(header_path, manifest_path, checker_hash):
    """Only replace generated reproduction guidance, preserving shader bytes."""
    header = header_path.read_text()
    old = '// Reproduce: python3 tools/shaders/generate_rigid_motion_pixel.py --check'
    if header.count(old) != 1:
        raise RuntimeError('Unexpected shared-generator header annotation')
    command = REPRODUCE + (' --compare-fused' if 'fused' in header_path.name else '')
    header_path.write_text(header.replace(old, '// Reproduce: ' + command))
    manifest = json.loads(manifest_path.read_text())
    manifest['header_sha256'] = digest(header_path)
    tool = str(Path(__file__).resolve().relative_to(ROOT))
    manifest['header_annotation_tool'] = tool
    manifest['tool_sources'][tool] = checker_hash
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--d3dx', type=Path, default=Path.home() /
        'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--summary', type=Path)
    parser.add_argument('--compare-fused', action='store_true')
    args = parser.parse_args()
    if generator.game_running():
        raise RuntimeError('X3AP running or process inventory failed; postpone compilation')
    output = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(
        prefix='x3-bloom-composition-compile-'))
    output.mkdir(parents=True, exist_ok=True)
    summary = args.summary or output / 'summary.json'
    summary.parent.mkdir(parents=True, exist_ok=True)
    kernels = {'bloom_agx': ROOT / 'src/temporal/bloom_agx_ps.hlsl'}
    if args.compare_fused:
        kernels['bloom_agx_fused'] = Path(__file__).with_name('bloom_agx_fused_comparison_ps.hlsl')
    inputs = {Path(__file__).resolve(), GENERATOR, generator.COMPILER_SOURCE,
              args.d3dx.resolve(), PROFILE_SOURCE, INDEX_SOURCE,
              Path(__file__).with_name('bloom_shader_limits.py').resolve(),
              Path(__file__).with_name('game_guard.py').resolve()}
    for name, source in kernels.items():
        _, includes = generator.expand_includes(source)
        inputs.update([source.resolve(), *includes])
        generator.SHADERS[name] = dict(source=source.resolve(),
            header=output / (name + '_program_inc.h'),
            provenance=output / (name + '_program.json'))
    before = {str(path): digest(path) for path in sorted(inputs)}
    records = []
    report = dict(schema=1, passed=False, phase='running', retained_dir=str(output),
        scope='Native authored shader compilation and conservative static SM3 budgets only',
        game_launched=False, creates_d3d_device=False, gpu_execution_verified=False,
        integrated_into_renderer=False, installed_dll_changed=False, bottle='Steam',
        inputs_before=before, kernels=records)
    summary.write_text(json.dumps(report, indent=2) + '\n')
    args.check = False
    try:
        for name in kernels:
            report['current_kernel'] = name
            item = generator.compile_one(name, args)
            config = generator.SHADERS[name]
            manifest = annotate(config['header'], config['provenance'], before[str(Path(__file__).resolve())])
            item.update(retained_header=str(config['header']),
                        retained_manifest=str(config['provenance']),
                        retained_manifest_sha256=digest(config['provenance']), provenance=manifest)
            records.append(item)
            words = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{8})u', config['header'].read_text())]
            code = struct.pack('<' + 'I' * len(words), *words)
            if len(words) != item['word_count'] or hashlib.sha256(code).hexdigest() != item['bytecode_sha256']:
                raise RuntimeError('Retained header differs from native compiled bytecode')
            item['sm3_budget'] = check_bytecode(code)
        after = {path: digest(path) for path in before}
        if before != after:
            raise RuntimeError('Composition compilation inputs changed')
        report.update(passed=True, phase='complete')
        report.pop('current_kernel', None)
    except Exception as error:
        report.update(phase='failed', error=str(error))
        raise
    finally:
        after = {path: digest(path) if Path(path).is_file() else None for path in before}
        report.update(inputs_after=after, inputs_stable=before == after)
        summary.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(dict(passed=report['passed'], summary=str(summary), kernels=len(records))))


if __name__ == '__main__':
    main()
