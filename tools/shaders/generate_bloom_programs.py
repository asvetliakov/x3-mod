#!/usr/bin/env python3
"""Package nine authored bloom shaders; existing ten programs are untouched.

Native generation and --check MUST run through verification/probe/wine_lock.py.
--promote-verified performs no Wine calls: it verifies retained native records,
current inputs and bytecode before promotion, preserving their provenance.
--check recompiles exactly nine bloom programs, compares embedded bytecode and
canonical build provenance, and retains any historical promotion evidence.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import bottle
from bloom_shader_limits import check_bytecode

GENERATOR = Path(__file__).with_name('generate_rigid_motion_pixel.py')
spec = importlib.util.spec_from_file_location('bloom_native_generator', GENERATOR)
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
NAMES = ('extract_gamma', 'extract_srgb', 'extract_none', 'extract_even_gamma',
         'extract_even_srgb', 'extract_even_none', 'down', 'up', 'agx')
SHADERS = {f'bloom_{name}': dict(source=ROOT / f'src/temporal/bloom_{name}_ps.hlsl',
    header=ROOT / f'src/renderer/bloom_{name}_program_inc.h',
    provenance=ROOT / f'verification/results/bloom-{name.replace("_", "-")}-program.json') for name in NAMES}
REPRODUCE = ('python3 verification/probe/wine_lock.py python3 '
             'tools/shaders/generate_bloom_programs.py --check')
CORE_FIELDS = ('schema', 'source', 'source_sha256', 'compiler', 'compiler_sha256',
    'entry', 'target', 'flags', 'flags_name', 'defines', 'includes', 'word_count',
    'bytecode_sha256', 'copyright_scope', 'creates_d3d_device')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest(path):
    return sha(Path(path).read_bytes())


def read_code(header):
    words = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})u', header)]
    code = struct.pack('<' + 'I' * len(words), *words)
    native.validate(code)
    check_bytecode(code)
    return code


def current_tools():
    return {str(p.relative_to(ROOT)): digest(p) for p in
        (native.COMPILER_SOURCE, GENERATOR, Path(__file__).resolve())}


def package(source, code, original):
    """Canonical reproducible header/build record; no compiler relabeling."""
    words = native.validate(code)
    check_bytecode(code)
    if original['bytecode_sha256'] != sha(code) or original['word_count'] != len(words):
        raise ValueError('Native word identity mismatch')
    header = f'// Generated from our original {source.relative_to(ROOT)}. Do not edit.\n'
    header += '// Reproduce: ' + REPRODUCE + '\n'
    header += ''.join('    ' + ', '.join(f'0x{v:08x}u' for v in words[i:i+6]) + ',\n'
                      for i in range(0, len(words), 6))
    record = {k: original[k] for k in CORE_FIELDS}
    record.update(header_sha256=sha(header.encode()), tool_sources=current_tools(),
        header_annotation_tool=str(Path(__file__).resolve().relative_to(ROOT)))
    return header, record


def verify_inputs(report):
    before = report['inputs_before']
    if not report.get('inputs_stable') or before != report.get('inputs_after'):
        raise ValueError('Retained compilation inputs were unstable')
    for path, expected in before.items():
        if digest(path) != expected:
            raise ValueError('Retained compilation input changed: ' + path)


def verify_retained(item, config, compiler):
    """Match retained records/artifacts AND current source/include/tool content."""
    manifest_path, header_path = Path(item['retained_manifest']), Path(item['retained_header'])
    raw = manifest_path.read_bytes()
    if sha(raw) != item['retained_manifest_sha256']:
        raise ValueError('Retained manifest hash mismatch')
    record = json.loads(raw)
    if record != item['provenance'] or digest(header_path) != record['header_sha256']:
        raise ValueError('Retained provenance/header mismatch')
    source = config['source']
    _, includes = native.expand_includes(source)
    if record['source'] != str(source.relative_to(ROOT)) or record['source_sha256'] != digest(source):
        raise ValueError('Retained source mismatch')
    expected_includes = {str(p.relative_to(ROOT)): digest(p) for p in includes} or None
    if record['includes'] != expected_includes or record['compiler_sha256'] != digest(compiler):
        raise ValueError('Retained include/compiler mismatch')
    for name, expected in record['tool_sources'].items():
        if digest(ROOT / name) != expected:
            raise ValueError('Retained tool changed: ' + name)
    if record['entry'] != 'main' or record['target'] != 'ps_3_0' or record['flags'] != 32768:
        raise ValueError('Unexpected native compilation contract')
    code = read_code(header_path.read_text())
    if sha(code) != record['bytecode_sha256'] or sha(code) != item['bytecode_sha256']:
        raise ValueError('Retained bytecode mismatch')
    return code, record, sha(raw)


def promotion_inputs(args, watched):
    def watch(paths):
        for value in paths:
            path = Path(value).resolve()
            if path not in watched:
                watched[path] = digest(path)
    filter_path = ROOT / 'verification/results/bloom-filter-compile.json'
    composition_path = ROOT / 'verification/results/bloom-composition-compile.json'
    watch((filter_path, composition_path))
    filters = json.loads(filter_path.read_text())
    if not filters.get('passed') or filters.get('phase') != 'complete':
        raise ValueError('Filter compilation did not pass')
    watch(filters['inputs_before'])
    verify_inputs(filters)
    composition = json.loads(composition_path.read_text())
    if composition.get('selected_program_passed') is not True or composition.get('fused_program_admitted') is not False:
        raise ValueError('Unexpected candidate/fused comparison verdict')
    # The comparison intentionally fails its fused budget. Only the explicitly
    # successful selected shader is promoted and its budget is recomputed here.
    watch((composition['original_record'],))
    if digest(composition['original_record']) != composition['original_record_sha256']:
        raise ValueError('Original composition record changed')
    original = json.loads(Path(composition['original_record']).read_text())
    if original != composition['comparison']:
        raise ValueError('Composition wrapper differs from original evidence')
    watch(original['inputs_before'])
    verify_inputs(original)
    selected = [i for i in original['kernels'] if i['shader'] == 'bloom_agx']
    if len(selected) != 1 or selected[0].get('sm3_budget', {}).get('passed') is not True:
        raise ValueError('Selected candidate budget absent/failed')
    items = [(i, filter_path) for i in filters['kernels']] + [(selected[0], composition_path)]
    if len(items) != len(SHADERS) or {i['shader'] for i, _ in items} != set(SHADERS):
        raise ValueError('Retained shader set differs from production bundle')
    output = {}
    for item, summary in items:
        name = item['shader']
        watch((item['retained_manifest'], item['retained_header']))
        code, original, manifest_hash = verify_retained(item, SHADERS[name], args.d3dx)
        header, record = package(SHADERS[name]['source'], code, original)
        record['promotion'] = dict(kind='verified retained native compilation; no recompilation during promotion',
            summary=str(summary.relative_to(ROOT)), summary_sha256=digest(summary),
            native_manifest_sha256=manifest_hash, native_provenance=original)
        output[name] = header, record
    return output


def generated_inputs(args):
    if native.game_running():
        raise RuntimeError('X3AP running or process inventory failed; postpone compilation')
    output = {}
    with tempfile.TemporaryDirectory(prefix='x3-bloom-production-') as directory:
        for name, config in SHADERS.items():
            header, manifest = Path(directory) / (name + '.h'), Path(directory) / (name + '.json')
            native.SHADERS[name] = dict(source=config['source'], header=header, provenance=manifest)
            # compile_one's check compares its historical annotation; compile
            # into scratch then compare our canonical form only after validation.
            compile_args = argparse.Namespace(d3dx=args.d3dx, check=False)
            native.compile_one(name, compile_args)
            output[name] = package(config['source'], read_code(header.read_text()), json.loads(manifest.read_text()))
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--promote-verified', action='store_true')
    parser.add_argument('--d3dx', type=Path, default=bottle.game_dir('X3') / 'd3dx9_37.dll',
        help='D3DX9 compiler DLL (default: %(default)s)')
    args = parser.parse_args()
    watched = {GENERATOR, native.COMPILER_SOURCE, Path(__file__).resolve(), args.d3dx.resolve(),
        ROOT / 'verification/probe/bloom_shader_limits.py',
        ROOT / 'tools/analysis/inspect_motion_output_profiles.py', ROOT / 'tools/analysis/index_shaders.py'}
    for config in SHADERS.values():
        _, includes = native.expand_includes(config['source'])
        watched.update([config['source'], *includes])
    before = {p: digest(p) for p in watched}
    output = promotion_inputs(args, before) if args.promote_verified else generated_inputs(args)
    if before != {p: digest(p) for p in before}:
        raise RuntimeError('Bloom packaging inputs changed during verification')
    # Verify/compile the complete set before changing any embedded artifact.
    for name, (header, record) in output.items():
        config = SHADERS[name]
        if args.check:
            retained = json.loads(config['provenance'].read_text())
            retained.pop('promotion', None) # historical proof preserved, not a new compiler input
            if config['header'].read_text() != header or retained != record:
                raise ValueError(name + ': embedded program/provenance differs from native compilation')
        else:
            config['header'].write_text(header)
            config['provenance'].write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(dict(passed=True, programs=len(output), mode='check' if args.check else
        'verified-promotion' if args.promote_verified else 'native-generation',
        game_launched=False, old_programs_recompiled=False)))


if __name__ == '__main__':
    main()
