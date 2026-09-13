#!/usr/bin/env python3
"""Stage authored bloom bytecode and promote the same GPU-qualified artifacts.

Compilation runs only through wine_lock.py; X3M_FIXTURE_BOTTLE selects the bottle.
Promotion executes no Wine/compiler/DLL build. Historical generator promotion
remains separate. The single artifact record binds current inputs, baseline,
new bytecode and the GPU fixture's matching shader hashes.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import bottle
from run_bloom_filter import require_runner_lock
from bloom_shader_limits import check_bytecode, PROFILE_SOURCE, INDEX_SOURCE

spec = importlib.util.spec_from_file_location('bloom_packaging',
    Path(__file__).with_name('generate_bloom_programs.py'))
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
SHARED = (('quad_vertex', 'quad_vs'), ('taa_sharpen', 'taa_sharpen_ps'),
          ('hdr_writeback', 'hdr_writeback_ps'))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_words(path):
    values = [int(x, 16) for x in re.findall(r'0x([0-9a-f]{8})u', Path(path).read_text())]
    return struct.pack('<' + 'I' * len(values), *values)


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def inputs(compiler):
    """Snapshot the entire batch before the first compile, including baseline."""
    paths = {Path(__file__).resolve(), Path(pack.__file__).resolve(),
             Path(pack.native.__file__).resolve(), pack.native.COMPILER_SOURCE,
             ROOT / 'verification/probe/bloom_shader_limits.py', PROFILE_SOURCE, INDEX_SOURCE,
             ROOT / 'verification/probe/run_bloom_filter.py',
             ROOT / 'verification/probe/game_guard.py', ROOT / 'verification/probe/wine_lock.py',
             Path(bottle.__file__).resolve(), compiler.resolve()}
    for config in pack.SHADERS.values():
        paths.update((config['source'], config['header'], config['provenance']))
        paths.update(pack.native.expand_includes(config['source'])[1])
    for base, _ in SHARED:
        paths.add(ROOT / f'src/renderer/{base}_program_inc.h')
    return {str(path): digest(path) for path in sorted(paths)}


def unchanged(expected):
    return all(Path(path).is_file() and digest(path) == value
               for path, value in expected.items())


def compile_stage(directory, compiler):
    require_runner_lock()
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        raise ValueError('Artifact directory must be empty')
    # No compilation or baseline copy precedes this whole-batch snapshot.
    before = inputs(compiler)
    for group in ('baseline', 'new', 'native'):
        (directory / group).mkdir()
    report = dict(schema=1, passed=False, bottle=bottle.describe(),
        compiler=str(compiler), compiler_sha256=before[str(compiler.resolve())],
        inputs_before=before, kernels=[], baseline_headers={}, csos={},
        creates_d3d_device=False, gpu_execution_verified=False)
    report_path = directory / 'artifacts.json'
    save(report_path, report)
    try:
        for base, name in SHARED:
            header = ROOT / f'src/renderer/{base}_program_inc.h'
            code = read_words(header)
            report['baseline_headers'][name] = dict(path=str(header), sha256=before[str(header)])
            for group in ('baseline', 'new'):
                (directory / group / (name + '.cso')).write_bytes(code)
        for name, config in pack.SHADERS.items():
            report['current_kernel'] = name
            old = read_words(config['header'])
            prior = json.loads(config['provenance'].read_text())
            if prior['compiler_sha256'] != report['compiler_sha256'] or prior['flags'] != 32768:
                raise ValueError('Baseline compiler/flags mismatch: ' + name)
            if hashlib.sha256(old).hexdigest() != prior['bytecode_sha256']:
                raise ValueError('Baseline bytecode mismatch: ' + name)
            stem = name + '_ps'
            (directory / 'baseline' / (stem + '.cso')).write_bytes(old)
            report['baseline_headers'][stem] = dict(path=str(config['header']),
                sha256=before[str(config['header'])], record=str(config['provenance']),
                record_sha256=before[str(config['provenance'])])
            native_header = directory / 'native' / config['header'].name
            native_record = directory / 'native' / config['provenance'].name
            pack.native.SHADERS[name] = dict(source=config['source'],
                header=native_header, provenance=native_record)
            pack.native.compile_one(name, argparse.Namespace(d3dx=compiler, check=False))
            code = read_words(native_header)
            budget, old_budget = check_bytecode(code), check_bytecode(old)
            canonical, record = pack.package(config['source'], code,
                                             json.loads(native_record.read_text()))
            staged_header = directory / 'new' / config['header'].name
            staged_record = directory / 'new' / config['provenance'].name
            staged_header.write_text(canonical)
            save(staged_record, record)
            (directory / 'new' / (stem + '.cso')).write_bytes(code)
            report['kernels'].append(dict(name=name, baseline_dwords=len(old)//4,
                new_dwords=len(code)//4, dword_delta=(len(code)-len(old))//4,
                baseline_slots=old_budget['instruction_slots'], new_slots=budget['instruction_slots'],
                slot_delta=budget['instruction_slots']-old_budget['instruction_slots'], budget=budget,
                bytecode_sha256=record['bytecode_sha256'], header=str(staged_header),
                header_sha256=digest(staged_header), record=str(staged_record),
                record_sha256=digest(staged_record)))
            save(report_path, report)
        if not unchanged(before):
            raise ValueError('Inputs changed across compilation batch')
        report['csos'] = {str(path): digest(path)
                          for group in ('baseline', 'new')
                          for path in sorted((directory / group).glob('*.cso'))}
        if len(report['csos']) != 24:
            raise ValueError('Incomplete old/new twelve-program bundles')
        report.pop('current_kernel', None)
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['inputs_stable'] = unchanged(before)
        save(report_path, report)
    return report


def promote(directory, gpu_summary):
    report = json.loads((directory / 'artifacts.json').read_text())
    gpu_raw = gpu_summary.read_bytes()
    gpu_hash = hashlib.sha256(gpu_raw).hexdigest()
    gpu = json.loads(gpu_raw)
    if not report['passed'] or not report['inputs_stable'] or not gpu['passed'] \
            or not gpu['gpu_execution_verified'] or not gpu.get('inputs_unchanged'):
        raise ValueError('Missing passed stable compile/GPU evidence')
    if not unchanged(report['inputs_before']) or not unchanged(report['csos']):
        raise ValueError('Compilation inputs or retained CSOs changed')
    if {item['name'] for item in report['kernels']} != set(pack.SHADERS) \
            or len(report['kernels']) != len(pack.SHADERS):
        raise ValueError('Incomplete/duplicate staged shader set')
    for item in report['kernels']:
        name = item['name']
        if gpu['compiled_shaders'][name + '_ps'] != item['bytecode_sha256']:
            raise ValueError('GPU used different bytecode: ' + name)
        if digest(item['header']) != item['header_sha256'] \
                or digest(item['record']) != item['record_sha256']:
            raise ValueError('Staged artifact changed')
        if hashlib.sha256(read_words(item['header'])).hexdigest() != item['bytecode_sha256']:
            raise ValueError('Staged header/bytecode mismatch')
    # Validate the whole set before writing any production include or record.
    for item in report['kernels']:
        config = pack.SHADERS[item['name']]
        config['header'].write_bytes(Path(item['header']).read_bytes())
        config['provenance'].write_bytes(Path(item['record']).read_bytes())
    promotion = dict(promoted=len(report['kernels']),
        shaders=sorted(item['name'] for item in report['kernels']),
        gpu_summary=str(gpu_summary), gpu_summary_sha256=gpu_hash,
        gpu_passed=True, gpu_execution_verified=True, gpu_inputs_unchanged=True,
        recompiled=False, dll_built=False)
    report['promotion'] = promotion
    save(directory / 'artifacts.json', report)
    return promotion


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=('compile', 'promote'))
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--gpu-summary', type=Path)
    parser.add_argument('--d3dx', type=Path, default=bottle.game_dir() / 'd3dx9_37.dll')
    args = parser.parse_args()
    directory = args.directory.resolve()
    if args.phase == 'compile':
        report = compile_stage(directory, args.d3dx.resolve())
        result = dict(artifacts=str(directory / 'artifacts.json'),
                      kernels=len(report['kernels']), passed=report['passed'])
    else:
        if args.gpu_summary is None:
            parser.error('Promotion requires --gpu-summary')
        result = promote(directory, args.gpu_summary.resolve())
    print(json.dumps(result))


if __name__ == '__main__':
    main()
