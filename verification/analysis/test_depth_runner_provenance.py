"""Offline failure-path tests for fresh-build depth verification runners."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class DepthRunnerProvenanceTests(unittest.TestCase):
    def exercise(self, kind, scenario):
        filename = f'verification/probe/run_depth_{kind}.py'
        spec = importlib.util.spec_from_file_location(f'depth_runner_{kind}', ROOT / filename)
        runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(runner)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            names = [filename, f'verification/probe/depth_{kind}.cpp',
                     f'verification/probe/build_depth_{kind}.sh', 'src/temporal/depth_decode.hlsl',
                     f'verification/probe/build/depth_{kind}.exe']
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('stale executable' if name.endswith('.exe') else 'original source')
            runner.__file__ = str(root / filename)
            launches = []
            def run(command, **kwargs):
                if command[0] == 'sh':
                    if scenario == 'build_failure':
                        return subprocess.CompletedProcess(command, 1, '', 'intentional failure')
                    (root / names[-1]).write_text('fresh executable')
                    if scenario == 'build_mutation':
                        (root / names[1]).write_text('changed while building')
                    return subprocess.CompletedProcess(command, 0, '', '')
                launches.append(command)
                if kind == 'decode':
                    lines = (['SAMPLE error_d24_lsb=0 PASS'] * 256 + ['COPY_STATE PASS'] * 4 +
                             ['RESTORE PASS'] * 8 + ['TIMING width=64'] * 4 + ['RESET PASS', 'RESULT PASS:'])
                else:
                    lines = (['SAMPLE PASS'] * 36 + ['STATE PASS'] * 6 +
                             ['Reset after resource release: 0x00000000 OK', 'RESULT PASS:'])
                kwargs['stdout'].write('\n'.join(lines))
                if scenario == 'run_source_mutation':
                    (root / names[1]).write_text('changed while running')
                if scenario == 'run_executable_mutation':
                    (root / names[-1]).write_text('different executable')
                return subprocess.CompletedProcess(command, 0)
            argv = [filename] + (['--case', 'D24S8_INTZ'] if kind == 'resolve' else [])
            with patch.object(runner.subprocess, 'run', side_effect=run), patch('sys.argv', argv), \
                    contextlib.redirect_stdout(io.StringIO()):
                code = runner.main()
            stem = 'depth-decode' if kind == 'decode' else 'depth-resolve-d24s8-intz'
            report = json.loads((runner.bottle.results_dir(root) / f'{stem}-summary.json').read_text())
            self.assertEqual(code, 0 if scenario == 'stable' else 1)
            self.assertEqual(report['passed'], scenario == 'stable')
            self.assertEqual(len(launches), 0 if scenario.startswith('build_') else 1)
            if scenario == 'stable':
                self.assertTrue(report['freshly_built'])
                self.assertTrue(report['sources_unchanged_after_build'])
                self.assertTrue(report['sources_unchanged_after_run'])
                self.assertTrue(report['executable_unchanged_after_run'])

    def test_stable_fresh_build(self):
        for kind in ('decode', 'resolve'):
            with self.subTest(kind=kind): self.exercise(kind, 'stable')

    def test_failed_build_does_not_run_stale_executable(self):
        for kind in ('decode', 'resolve'):
            with self.subTest(kind=kind): self.exercise(kind, 'build_failure')

    def test_source_change_during_build_rejects_run(self):
        for kind in ('decode', 'resolve'):
            with self.subTest(kind=kind): self.exercise(kind, 'build_mutation')

    def test_source_change_during_run_rejects_evidence(self):
        for kind in ('decode', 'resolve'):
            with self.subTest(kind=kind): self.exercise(kind, 'run_source_mutation')

    def test_executable_change_during_run_rejects_evidence(self):
        for kind in ('decode', 'resolve'):
            with self.subTest(kind=kind): self.exercise(kind, 'run_executable_mutation')


if __name__ == '__main__':
    unittest.main()
