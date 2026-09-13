"""Production bundle provenance and tamper controls; no Wine/device calls."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('production_bloom_generator',
    ROOT / 'tools/shaders/generate_bloom_programs.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
COMPILER = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'


class BloomProgramsTests(unittest.TestCase):
    def packaged_item(self):
        name, config = next(iter(tool.SHADERS.items()))
        record = json.loads(config['provenance'].read_text())
        return dict(shader=name, retained_header=str(config['header']),
            retained_manifest=str(config['provenance']),
            retained_manifest_sha256=tool.digest(config['provenance']),
            bytecode_sha256=record['bytecode_sha256'], provenance=record)

    def test_all_nine_embedded_records_match_words_and_current_inputs(self):
        self.assertEqual(len(tool.SHADERS), 9)
        for name, config in tool.SHADERS.items():
            with self.subTest(shader=name):
                manifest = json.loads(config['provenance'].read_text())
                text = config['header'].read_text()
                code = tool.read_code(text)
                self.assertEqual(tool.sha(code), manifest['bytecode_sha256'])
                self.assertEqual(len(code) // 4, manifest['word_count'])
                self.assertEqual(tool.digest(config['header']), manifest['header_sha256'])
                self.assertEqual(tool.digest(config['source']), manifest['source_sha256'])
                for path, value in (manifest['includes'] or {}).items():
                    self.assertEqual(tool.digest(ROOT / path), value)
                for path, value in manifest['tool_sources'].items():
                    self.assertEqual(tool.digest(ROOT / path), value)
                self.assertEqual(manifest['compiler_sha256'], tool.digest(COMPILER))
                self.assertIn(tool.REPRODUCE, text)

    def test_promoted_native_identity_is_preserved_in_canonical_record(self):
        for config in tool.SHADERS.values():
            record = json.loads(config['provenance'].read_text())
            native = record.get('promotion', {}).get('native_provenance')
            if native is None: # a later genuine regeneration may replace promotion
                continue
            self.assertEqual({k: record[k] for k in tool.CORE_FIELDS},
                             {k: native[k] for k in tool.CORE_FIELDS})
            expected, canonical = tool.package(config['source'],
                tool.read_code(config['header'].read_text()), native)
            self.assertEqual(expected, config['header'].read_text())
            self.assertEqual(canonical, {k: v for k, v in record.items() if k != 'promotion'})

    def test_retained_header_tamper_refused_before_packaging(self):
        item = self.packaged_item()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'tampered.h'
            original = Path(item['retained_header']).read_text()
            path.write_text(original.replace('0xffff0300u', '0xffff0200u', 1))
            item['retained_header'] = str(path)
            with self.assertRaisesRegex(ValueError, 'provenance/header'):
                tool.verify_retained(item, tool.SHADERS[item['shader']], COMPILER)

    def test_retained_source_drift_refused_even_with_updated_manifest_hash(self):
        item = self.packaged_item()
        with tempfile.TemporaryDirectory() as directory:
            manifest = copy.deepcopy(item['provenance'])
            manifest['source_sha256'] = '0' * 64
            path = Path(directory) / 'changed.json'
            path.write_text(json.dumps(manifest))
            item.update(retained_manifest=str(path), retained_manifest_sha256=tool.digest(path), provenance=manifest)
            with self.assertRaisesRegex(ValueError, 'source mismatch'):
                tool.verify_retained(item, tool.SHADERS[item['shader']], COMPILER)

    def test_unstable_or_changed_compilation_inputs_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input'
            path.write_text('first')
            hashes = {str(path): tool.digest(path)}
            report = dict(inputs_before=hashes, inputs_after=hashes, inputs_stable=False)
            with self.assertRaisesRegex(ValueError, 'unstable'):
                tool.verify_inputs(report)
            report['inputs_stable'] = True
            path.write_text('second')
            with self.assertRaisesRegex(ValueError, 'input changed'):
                tool.verify_inputs(report)


if __name__ == '__main__':
    unittest.main()
