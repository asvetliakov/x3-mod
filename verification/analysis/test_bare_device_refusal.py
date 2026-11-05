"""The bare-route refusal: create_device hooks only the ownership wrapper's device.

Pins the production gate in src/proxy/capture.cpp and the native record of run_bare_device_refusal.py
(verification/results/native-windows/bare-device-refusal/summary.json): with X3M_OWNERSHIP=0 the native device came back
unhooked (no slot target in the proxy image), SetRenderTarget and the frame/Reset calls succeeded on Windows' d3d9.dll
and the log carried one refusal row; the ownership control was hooked. The record is not bound to source hashes (the
host fixture test_capture_device_creation covers the control flow on every tree).
"""
import json
import re
import tempfile
import unittest
from pathlib import Path

from source_text import source_text
import test_taa_thin_vote as thin_vote  # its dry-run launch harness (module import: its tests are not collected here)

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / 'verification/results/native-windows/bare-device-refusal/summary.json'
ROW = 'device_hook route=bare refused reason=vtable_copy_unsafe'


class BareDeviceRefusalTests(unittest.TestCase):
    def test_create_device_gates_the_hook_on_the_wrapper(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        body = capture[capture.index('HRESULT WINAPI create_device('):]
        body = body[:body.index('\n}\n')]
        gate = re.search(r'if\s*\(ownership::borrowed_native_device\(\*out\)\)\s*hook_device\(\*out,', body)
        self.assertIsNotNone(gate)
        self.assertIn(f'log("{ROW} ptr=%p"', body[gate.end():])
        self.assertEqual(body.count('hook_device('), 1)  # no other route to the device hooks in create_device

    def test_native_record(self):
        record = json.loads(RECORD.read_text())
        self.assertTrue(record['passed'], record['problems'])
        self.assertFalse(record['game_launched'])
        self.assertEqual(record['host']['name'], 'native-windows')
        bare, owned = record['runs']['bare'], record['runs']['ownership']
        for run in (bare, owned):
            self.assertEqual((run['exit'], run['calls'], run['failed_calls'], run['result']), (0, 24, [], 'PASS failures=0'))
            self.assertIsNone(run['exception'])
        for tag in ('SLOTS', 'SLOTS_AFTER'):
            self.assertEqual(bare['slots'][tag]['proxy'], 0)
            self.assertEqual(owned['slots'][tag], {'proxy': 119, 'system': 0, 'other': 0})
        self.assertEqual((bare['refused_rows'], bare['device_hooked_rows'], bare['ownership_factory']), (1, 0, []))
        self.assertEqual((owned['refused_rows'], owned['device_hooked_rows'], owned['ownership_factory']), (0, 1, ['wrapped']))


class OwnershipOffLaunchLine(unittest.TestCase):
    """The launcher's dry run: --no-ownership sends X3M_OWNERSHIP=0 with one line saying the renderer is off."""
    LINE = 'ownership off: the renderer is disabled on every host (the device is forwarded unhooked, as vanilla)'

    def test_line_only_when_ownership_is_off(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, lines in ((['--no-ownership'], [self.LINE]), ([], []), (['--vanilla'], [])):
                code, output, error = thin_vote.ThinVoteLaunch.launch(self, directory, *args)
                self.assertEqual(code, 0, error)
                self.assertEqual([l for l in error.splitlines() if l.startswith('ownership off')], lines, args)
                if not args:
                    self.assertEqual(json.loads(output)['env']['X3M_OWNERSHIP'], '1')
                if args == ['--no-ownership']:
                    self.assertEqual(json.loads(output)['env']['X3M_OWNERSHIP'], '0')


if __name__ == '__main__':
    unittest.main()
