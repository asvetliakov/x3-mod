"""Runner rejection behavior, independent of building or executing a probe."""
import ast
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'verification/probe/run_finite_buffer_evidence.py'


class FiniteEvidenceRunnerTests(unittest.TestCase):
    def test_exact_terminal(self):
        tree = ast.parse(RUNNER.read_text())
        node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'terminal_result')
        scope = {'re': re}
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(RUNNER), 'exec'), scope)
        parse = scope['terminal_result']
        valid = 'RESULT PASS checks=214651 all_atlases_released=1'
        self.assertIsNotNone(parse('diagnostic\n' + valid + '\n'))
        for invalid in ('', valid + '\n' + valid + '\n', valid + '\nRESULT FAIL bad\n',
                        'RESULT FAIL bad\n' + valid + '\n', valid + '\ntrailing output\n',
                        valid + ' extra\n', valid + '\n\n'):
            with self.subTest(invalid=invalid):
                self.assertIsNone(parse(invalid))

    def test_missing_source_replaces_previous_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            probe = root / 'verification/probe'
            result = root / 'verification/results'
            probe.mkdir(parents=True)
            result.mkdir(parents=True)
            runner = probe / RUNNER.name
            shutil.copyfile(RUNNER, runner)
            summary = result / 'finite-buffer-evidence-summary.json'
            summary.write_text(json.dumps({'passed': True, 'stale_marker': True}))
            run = subprocess.run([sys.executable, str(runner)], capture_output=True, text=True, timeout=10)
            self.assertNotEqual(run.returncode, 0)
            report = json.loads(summary.read_text())
            self.assertIs(report['passed'], False)
            self.assertNotIn('stale_marker', report)
            self.assertNotIn('artifacts', report)


if __name__ == '__main__':
    unittest.main()
