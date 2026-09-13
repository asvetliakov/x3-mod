"""Focused retained-artifact mutation and promotion gates; no compiler/Wine."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bloom_stage',
    ROOT / 'tools/shaders/stage_bloom_programs.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class StagingTests(unittest.TestCase):
    def test_snapshot_precedes_first_compile_and_detects_later_shader_drift(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve(); output = root / 'artifacts'; compiler = root / 'compiler'
            compiler.write_bytes(b'compiler')
            configs = {}
            for name in ('first', 'second'):
                source = root / (name + '.hlsl'); source.write_text('original source')
                header = root / (name + '.h'); header.write_text('0xffff0300u,0x0000ffffu')
                record = root / (name + '.json')
                stage.save(record, dict(compiler_sha256=stage.digest(compiler), flags=32768,
                    bytecode_sha256=hashlib.sha256(stage.read_words(header)).hexdigest()))
                configs[name] = dict(source=source, header=header, provenance=record)
            watched = {compiler, *(p for c in configs.values() for p in c.values())}
            initial = {str(p): stage.digest(p) for p in watched}; seen = []
            def compile_one(name, args):
                # The whole-batch snapshot must already exist before any compile.
                report = json.loads((output / 'artifacts.json').read_text())
                self.assertEqual(report['inputs_before'], initial)
                seen.append(name)
                target = stage.pack.native.SHADERS[name]
                target['header'].write_text('0xffff0300u,0x0000ffffu')
                stage.save(target['provenance'], {'bytecode_sha256':
                    hashlib.sha256(stage.read_words(target['header'])).hexdigest()})
                if name == 'first':
                    configs['second']['source'].write_text('changed before its compile')
            def package(source, code, record):
                return '0xffff0300u,0x0000ffffu', record
            with patch.object(stage, 'require_runner_lock'), \
                 patch.object(stage, 'inputs', return_value=initial), \
                 patch.object(stage, 'SHARED', ()), \
                 patch.dict(stage.pack.SHADERS, configs, clear=True), \
                 patch.object(stage.pack.native, 'compile_one', side_effect=compile_one), \
                 patch.object(stage.pack, 'package', side_effect=package), \
                 patch.object(stage, 'check_bytecode', return_value={'instruction_slots': 1}):
                with self.assertRaisesRegex(ValueError, 'changed across'):
                    stage.compile_stage(output, compiler)
            self.assertEqual(seen, ['first', 'second'])
            report = json.loads((output / 'artifacts.json').read_text())
            self.assertFalse(report['passed']); self.assertFalse(report['inputs_stable'])

    def test_promotion_validates_all_artifacts_before_any_source_write(self):
        for fault in ('none', 'input', 'cso', 'header', 'gpu_hash', 'unstable', 'duplicate'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve(); configs = {}; report = dict(passed=True, inputs_stable=True,
                    inputs_before={}, csos={}, kernels=[])
                gpu = dict(passed=True, gpu_execution_verified=True, inputs_unchanged=True,
                           compiled_shaders={})
                for name in ('first', 'second'):
                    dest = root / (name + '-production.h'); dest.write_text('old')
                    dest_record = root / (name + '-production.json'); dest_record.write_text('old record')
                    configs[name] = dict(header=dest, provenance=dest_record)
                    staged = root / (name + '-staged.h'); staged.write_text('0xffff0300u,0x0000ffffu')
                    record = root / (name + '-staged.json'); record.write_text('{}')
                    code = stage.read_words(staged); cso = root / (name + '.cso'); cso.write_bytes(code)
                    report['inputs_before'][str(dest)] = stage.digest(dest)
                    report['csos'][str(cso)] = stage.digest(cso)
                    sha = hashlib.sha256(code).hexdigest()
                    report['kernels'].append(dict(name=name, bytecode_sha256=sha,
                        header=str(staged), header_sha256=stage.digest(staged),
                        record=str(record), record_sha256=stage.digest(record)))
                    gpu['compiled_shaders'][name + '_ps'] = sha
                if fault == 'input': configs['second']['header'].write_text('drift')
                if fault == 'cso': (root / 'second.cso').write_bytes(b'drift')
                if fault == 'header': (root / 'second-staged.h').write_text('drift')
                if fault == 'gpu_hash': gpu['compiled_shaders']['second_ps'] = 'bad'
                if fault == 'unstable': gpu['inputs_unchanged'] = False
                if fault == 'duplicate': report['kernels'][1] = report['kernels'][0]
                stage.save(root / 'artifacts.json', report); stage.save(root / 'gpu.json', gpu)
                with patch.dict(stage.pack.SHADERS, configs, clear=True):
                    if fault == 'none':
                        expected_gpu_hash = stage.digest(root / 'gpu.json')
                        write_bytes = Path.write_bytes
                        def mutate_summary_after_first_write(path, data):
                            count = write_bytes(path, data)
                            if path == configs['first']['header']:
                                (root / 'gpu.json').write_text('unvalidated later bytes')
                            return count
                        with patch.object(Path, 'write_bytes', mutate_summary_after_first_write):
                            result = stage.promote(root, root / 'gpu.json')
                        self.assertNotEqual(stage.digest(root / 'gpu.json'), expected_gpu_hash)
                        self.assertEqual(result['promoted'], 2)
                        self.assertFalse(result['recompiled']); self.assertFalse(result['dll_built'])
                        saved = json.loads((root / 'artifacts.json').read_text())['promotion']
                        self.assertEqual(saved, result)
                        self.assertEqual(saved['gpu_summary_sha256'], expected_gpu_hash)
                        self.assertEqual(saved['shaders'], ['first', 'second'])
                        self.assertTrue(saved['gpu_passed'] and saved['gpu_execution_verified']
                                        and saved['gpu_inputs_unchanged'])
                        self.assertEqual(configs['first']['header'].read_text(), '0xffff0300u,0x0000ffffu')
                    else:
                        with self.assertRaises(ValueError): stage.promote(root, root / 'gpu.json')
                        self.assertEqual(configs['first']['header'].read_text(), 'old')
                        self.assertEqual(configs['first']['provenance'].read_text(), 'old record')


if __name__ == '__main__': unittest.main()
