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

    # The read-path evidence (TIMING per mode, one IDENTITY line with equal=1)
    # is part of a passing report since the engine_memory change.
    READ_PATH = (b'TIMING mode=rpm snapshot_us=9.000 reads_per_call=0.00 queries_per_call=0.0000 syscalls_per_call=12.00\n'
                 b'TIMING mode=direct snapshot_us=0.500 reads_per_call=12.00 queries_per_call=0.0200 syscalls_per_call=0.00\n'
                 b'IDENTITY rpm=0123456789abcdef direct=0123456789abcdef equal=1\n'
                 b'JOURNAL capacity=512 cycle_idle_us=1.0000 cycle_journal_us=1.0100 retirement_delta_us=0.0100 empty_drain_us=0.0200 drained=40000\n')
    RESULT = b'RESULT PASS checks=1 failures=0 backend_calls=1\n'

    def fake_run(self, output=READ_PATH + RESULT, mutate=None):
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
        result = self.READ_PATH + self.RESULT
        for output in (result + self.RESULT, result + b'trailing work\n', self.READ_PATH + b'noise ' + self.RESULT):
            with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(output)):
                self.assert_failed(RUNNER.run(self.root))

    def test_source_or_executable_change_rejected(self):
        for path in (self.exe, self.root / RUNNER.INPUTS[0]):
            with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(mutate=lambda: path.write_bytes(b'changed'))):
                self.assert_failed(RUNNER.run(self.root))

    def test_single_terminal_result_with_stable_inputs_passes(self):
        with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run()):
            result = RUNNER.run(self.root)
        self.assertTrue(result['passed'])
        self.assertEqual(result['read_path']['identity'][0]['equal'], '1')
        self.assertEqual([t['mode'] for t in result['read_path']['timing']], ['rpm', 'direct'])
        self.assertEqual(result['journal'][0]['capacity'], '512')

    def test_journal_measurement_required(self):
        lines = self.READ_PATH.split(b'\n')
        missing = b'\n'.join(l for l in lines if not l.startswith(b'JOURNAL ')) + self.RESULT
        partial = self.READ_PATH.replace(b' empty_drain_us=0.0200', b'') + self.RESULT
        for output in (missing, partial, self.READ_PATH + lines[3] + b'\n' + self.RESULT):
            with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(output)):
                self.assert_failed(RUNNER.run(self.root))

    def test_read_path_identity_required(self):
        unequal = self.READ_PATH.replace(b'equal=1', b'equal=0') + self.RESULT
        missing = self.RESULT
        one_mode = self.READ_PATH.split(b'\n', 1)[1] + self.RESULT
        for output in (unequal, missing, one_mode):
            with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(output)):
                self.assert_failed(RUNNER.run(self.root))


if __name__ == '__main__':
    unittest.main()
