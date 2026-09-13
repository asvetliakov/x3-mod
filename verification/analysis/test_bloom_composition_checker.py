"""Host provenance controls for the native composition checker; no Wine calls."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
spec = importlib.util.spec_from_file_location('bloom_composition_checker',
    ROOT / 'verification/probe/check_bloom_composition.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class CompositionCheckerTests(unittest.TestCase):
    def test_header_annotation_preserves_words_and_updates_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            for name in ('bloom_agx', 'bloom_agx_fused'):
                header = Path(directory) / (name + '_program_inc.h')
                manifest = Path(directory) / (name + '.json')
                words = '0xffff0300u, 0x0000ffffu\n'
                header.write_text('// Reproduce: python3 tools/shaders/generate_rigid_motion_pixel.py --check\n' + words)
                manifest.write_text(json.dumps(dict(header_sha256='old',
                    compiler_sha256='compiler', bytecode_sha256='bytecode', tool_sources={})))
                result = checker.annotate(header, manifest, 'checker')
                self.assertTrue(header.read_text().endswith(words))
                self.assertEqual(result['header_sha256'], checker.digest(header))
                self.assertEqual(result['compiler_sha256'], 'compiler')
                self.assertEqual(result['bytecode_sha256'], 'bytecode')
                self.assertEqual(result['tool_sources'][result['header_annotation_tool']], 'checker')
                self.assertEqual('--compare-fused' in header.read_text(), 'fused' in name)
                self.assertEqual(json.loads(manifest.read_text()), result)

    def test_unknown_annotation_fails_before_changing_header(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / 'test.h'
            header.write_text('// unexpected\n')
            with self.assertRaises(RuntimeError):
                checker.annotate(header, Path(directory) / 'absent.json', 'hash')
            self.assertEqual(header.read_text(), '// unexpected\n')

    def test_comparison_expands_authored_shader_and_shared_tail(self):
        source = ROOT / 'verification/probe/bloom_agx_fused_comparison_ps.hlsl'
        expanded, includes = checker.generator.expand_includes(source)
        expected = ['bloom_agx_ps.hlsl', 'agx.hlsl', 'bloom_common.hlsl', 'rcas.hlsl']
        self.assertEqual([p.name for p in includes], expected)
        self.assertIn('#define main bloomComposedTap', expanded)
        self.assertIn('#undef main', expanded)
        self.assertEqual(expanded.count('bloomComposedTap(uv'), 5)


if __name__ == '__main__':
    unittest.main()
