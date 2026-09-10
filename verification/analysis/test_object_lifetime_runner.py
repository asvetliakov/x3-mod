"""Runner provenance failures cannot retain a previous successful report."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('object_lifetime_runner', Path(__file__).parents[1] / 'probe/run_object_lifetime.py')
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class ObjectLifetimeRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.summary = self.root / 'verification/results/object-lifetime-summary.json'
        self.summary.parent.mkdir(parents=True)
        self.summary.write_text('{"passed":true}')
        for name in RUNNER.INPUTS:
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('original fixture input\n')
        self.exe = self.root / 'verification/probe/build/object_lifetime.exe'
        self.exe.parent.mkdir()
        self.exe.write_bytes(b'original synthetic executable')

    def fake_run(self, output=b'RESULT PASS checks=1 failures=0 backend_calls=1\n', mutate=None):
        def invoke(command, **kwargs):
            if command[0] == 'sh':
                return subprocess.CompletedProcess(command, 0, b'', b'')
            kwargs['stdout'].write(output)
            if mutate:
                mutate()
            return subprocess.CompletedProcess(command, 0)
        return invoke

    def assert_failed(self, result):
        self.assertFalse(result['passed'])
        self.assertFalse(json.loads(self.summary.read_text())['passed'])

    def test_missing_input_invalidates_stale_pass(self):
        (self.root / RUNNER.INPUTS[0]).unlink()
        self.assert_failed(RUNNER.run(self.root))

    def test_build_exception_invalidates_stale_pass(self):
        with patch.object(RUNNER.subprocess, 'run', side_effect=OSError('build unavailable')):
            self.assert_failed(RUNNER.run(self.root))

    def test_build_timeout_invalidates_stale_pass(self):
        with patch.object(RUNNER.subprocess, 'run', side_effect=subprocess.TimeoutExpired('build', 60)):
            self.assert_failed(RUNNER.run(self.root))

    def test_duplicate_or_nonterminal_result_rejected(self):
        result = b'RESULT PASS checks=1 failures=0 backend_calls=1\n'
        for output in (result + result, result + b'trailing work\n', b'noise ' + result):
            with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(output)):
                self.assert_failed(RUNNER.run(self.root))

    def test_source_or_executable_change_rejected(self):
        for path in (self.exe, self.root / RUNNER.INPUTS[0]):
            with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(mutate=lambda: path.write_bytes(b'changed'))):
                self.assert_failed(RUNNER.run(self.root))

    def test_single_terminal_result_with_stable_inputs_passes(self):
        with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run()):
            self.assertTrue(RUNNER.run(self.root)['passed'])


if __name__ == '__main__':
    unittest.main()
