import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import run_application_admission_abi as admission
import run_ownership_integration_fallback as fallback


class AdmissionHistoryTests(unittest.TestCase):
    def test_absent_history_is_explicitly_optional(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(admission.load_historical(Path(directory)), {
                'available': False, 'reason': 'no initial ABI record for this bottle'})

    def test_partial_history_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'application-admission-abi-initial.txt').write_text('orphan')
            with self.assertRaisesRegex(RuntimeError, 'incomplete historical ABI record'):
                admission.load_historical(path)

    def test_complete_history_keeps_provenance_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = admission.ROOT / 'verification/results/application-admission-abi-initial.txt'
            log = path / source.name
            log.write_bytes(source.read_bytes())
            digest = hashlib.sha256(log.read_bytes()).hexdigest()
            summary = {'passed': True, 'report_sha256': digest, 'executable_sha256': 'a' * 64,
                       'source_hashes_before_build': {'source.cpp': 'b' * 64},
                       'source_hashes_after_build': {'source.cpp': 'b' * 64},
                       'source_hashes_after_run': {'source.cpp': 'b' * 64}}
            (path / 'application-admission-abi-initial-summary.json').write_text(json.dumps(summary))
            result = admission.load_historical(path)
            self.assertTrue(result['available'])
            self.assertEqual(set(result['timing']), {'disabled', 'outer', 'nested'})

    def test_malformed_history_schema_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = admission.ROOT / 'verification/results/application-admission-abi-initial.txt'
            log = path / source.name
            log.write_bytes(source.read_bytes())
            digest = hashlib.sha256(log.read_bytes()).hexdigest()
            valid = {'passed': True, 'report_sha256': digest, 'executable_sha256': 'a' * 64,
                     'source_hashes_before_build': {'source.cpp': 'b' * 64},
                     'source_hashes_after_build': {'source.cpp': 'b' * 64},
                     'source_hashes_after_run': {'source.cpp': 'b' * 64}}
            cases = ({**valid, 'passed': 1},
                     {**valid, 'executable_sha256': None},
                     {**valid, 'source_hashes_before_build': None,
                      'source_hashes_after_build': None, 'source_hashes_after_run': None})
            for summary in cases:
                with self.subTest(summary=summary):
                    (path / 'application-admission-abi-initial-summary.json').write_text(json.dumps(summary))
                    with self.assertRaisesRegex(RuntimeError, 'invalid historical ABI summary schema'):
                        admission.load_historical(path)


class FallbackLinkTests(unittest.TestCase):
    def test_production_link_inputs_include_compositor_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'CMakeLists.txt').write_text(
                'add_library(d3d9 SHARED src/proxy/loader.cpp '
                'src/ownership/execution_state.cpp src/ownership/d3d9_ownership.cpp)')
            object_root = root / 'build-ownership/CMakeFiles/d3d9.dir'
            for relative in ('src/proxy/loader.cpp.obj', 'src/ownership/execution_state.cpp.obj'):
                path = object_root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            compositor = root / 'build-ownership/compositor_bridge'
            compositor.mkdir(parents=True)
            for name in ('compositor_bridge.o', 'compositor_bridge_seh_gnu.obj',
                         'libx3m_compositor_seh_runtime.a'):
                (compositor / name).touch()
            _, objects, generated, runtime = fallback.production_link_inputs(root)
            self.assertEqual([path.name for path in objects],
                             ['execution_state.cpp.obj', 'loader.cpp.obj'])
            self.assertEqual([path.name for path in generated],
                             ['compositor_bridge.o', 'compositor_bridge_seh_gnu.obj'])
            self.assertEqual(runtime.name, 'libx3m_compositor_seh_runtime.a')


if __name__ == '__main__':
    unittest.main()
