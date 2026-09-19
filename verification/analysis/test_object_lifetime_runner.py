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
        self.summary = RUNNER.bottle.results_dir(self.root) / 'object-lifetime-summary.json'
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
                 b'JOURNAL capacity=2048 cycle_idle_us=1.0000 cycle_journal_us=1.0100 retirement_delta_us=0.0100 empty_drain_us=0.0200 drained=40000\n'
                 b'X87 roundtrip_exact=0 save_stable=1 control_diff_slots=0xff control_exponent_diff=8'
                 b' control_reserved_diff=0 control_max_low_bits=64 control_st0=3fff8000000000000000/00000000000000000000'
                 b' control_ftw=0xc0/0xc0 compare_diff_slots=0xff compare_exponent_diff=80 compare_reserved_diff=0'
                 b' compare_max_low_bits=64 compare_ftw=0xc0/0xc0\n')
    READ_PATH += b''.join(b'JOURNAL_CASE name=%s result=PASS\n' % n.encode() for n in RUNNER.JOURNAL_CASES)
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
        self.assertEqual(result['journal'][0]['capacity'], '2048')
        self.assertFalse(result['x87_roundtrip_exact'])
        self.assertEqual(result['x87']['control_diff_slots'], '0xff')

    # The ST-slot comparison fidelity has to be reported, and an exact
    # environment must be recorded as exact rather than inherited from a lossy run.
    def test_x87_control_result_required_and_recorded(self):
        lines = self.READ_PATH.split(b'\n')
        x87 = next(l for l in lines if l.startswith(b'X87 '))
        missing = b'\n'.join(l for l in lines if not l.startswith(b'X87 ')) + self.RESULT
        duplicated = self.READ_PATH + x87 + b'\n' + self.RESULT
        unreported = self.READ_PATH.replace(b'roundtrip_exact=0', b'roundtrip_exact=unknown') + self.RESULT
        truncated = self.READ_PATH.replace(b' control_max_low_bits=64', b'') + self.RESULT
        for output in (missing, duplicated, unreported, truncated):
            with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(output)):
                self.assert_failed(RUNNER.run(self.root))
        exact = self.READ_PATH.replace(b'roundtrip_exact=0', b'roundtrip_exact=1') + self.RESULT
        with patch.object(RUNNER.subprocess, 'run', side_effect=self.fake_run(exact)):
            result = RUNNER.run(self.root)
        self.assertTrue(result['passed'])
        self.assertTrue(result['x87_roundtrip_exact'])

    def test_journal_measurement_required(self):
        lines = self.READ_PATH.split(b'\n')
        missing = b'\n'.join(l for l in lines if not l.startswith(b'JOURNAL ')) + self.RESULT
        partial = self.READ_PATH.replace(b' empty_drain_us=0.0200', b'') + self.RESULT
        failed_case = self.READ_PATH.replace(b'name=cost result=PASS', b'name=cost result=FAIL') + self.RESULT
        absent_case = self.READ_PATH.replace(b'JOURNAL_CASE name=cost result=PASS\n', b'') + self.RESULT
        for output in (missing, partial, failed_case, absent_case, self.READ_PATH + lines[3] + b'\n' + self.RESULT):
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
