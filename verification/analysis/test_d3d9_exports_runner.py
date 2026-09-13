"""Frozen-DLL runner contract, with every build/runtime subprocess mocked."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
import run_d3d9_exports as runner


class ExportRunnerTests(unittest.TestCase):
    def test_explicit_existing_dll_is_required(self):
        for arguments in ([], ['--dll', '/nonexistent/qualified/d3d9.dll']):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                runner.parse_args(arguments)
            self.assertEqual(error.exception.code, 2)

    def run_mocked(self, case, mutate=False):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary).resolve()
            dll = directory / 'qualified proxy.dll'
            exe = directory / 'fixture.exe'
            dll.write_bytes(b'qualified candidate')
            exe.write_bytes(b'fixture')
            def fake_case(name, readonly, report, selected, digest):
                self.assertEqual(selected, dll)
                self.assertEqual(digest, runner.sha(dll))
                self.assertEqual(readonly, name == 'readonly')
                if mutate:
                    dll.write_bytes(b'replaced candidate')
                return {'passed': True}
            with patch.object(runner, 'RESULTS', directory), patch.object(runner, 'EXE', exe), \
                    patch.object(runner, 'WINE', exe), patch.object(runner, 'game_running', return_value=[]), \
                    patch.object(runner.bottle, 'describe', return_value={'name': 'host-mock'}), \
                    patch.object(runner.pe_exports, 'parse', return_value={'names': sorted(runner.pe_exports.SYSTEM_D3D9_EXPORTS)}), \
                    patch.object(runner.subprocess, 'run') as build, \
                    patch.object(runner, 'run_case', side_effect=fake_case) as cases, contextlib.redirect_stdout(io.StringIO()):
                if mutate:
                    with self.assertRaisesRegex(AssertionError, 'Selected DLL changed during the run'):
                        runner.main(['--dll', str(dll), '--case', case])
                else:
                    runner.main(['--dll', str(dll), '--case', case])
                self.assertEqual(build.call_count, 1)
                self.assertEqual(build.call_args.args[0], ['sh', 'verification/probe/build_d3d9_exports.sh'])
                summary = json.loads((directory / 'd3d9-exports-summary.json').read_text())
                self.assertEqual(summary['status'], 'FAIL' if mutate else 'PASS')
                self.assertEqual([call.args[0] for call in cases.call_args_list],
                                 ['writable', 'readonly'] if case == 'both' else [case])

    def test_selected_smoke_only_builds_fixture(self):
        self.run_mocked('writable')

    def test_full_log_path_pair_remains_available(self):
        self.run_mocked('both')

    def test_readonly_can_run_independently(self):
        self.run_mocked('readonly')

    def test_candidate_replacement_is_rejected(self):
        self.run_mocked('writable', mutate=True)
