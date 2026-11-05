"""verification/probe/native_host.py: detection, the results directory, the task-list and `ver` parsers, the PE file
version, the fixture's identity rows -> the record's host block, WSLENV forwarding and the game-image refusal. Inline
fixtures only; nothing runs through interop."""
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import native_host  # noqa: E402


def fake_pe(major=10, minor=0, build=26100, patch=1):
    return b'MZ' + b'\0' * 62 + struct.pack('<IIII', 0xFEEF04BD, 0x10000, (major << 16) | minor, (build << 16) | patch) + b'\0' * 32


STDOUT = '\n'.join([
    'ADAPTER vendor=0x10de device=0x2684 subsys=0x16f310de revision=161 whql=1 driver_version=32.0.15.6094',
    'ADAPTER_DESC NVIDIA GeForce RTX 4090',
    'ADAPTER_DRIVER nvldumd.dll',
    r'D3D9_MODULE C:\Windows\SYSTEM32\d3d9.dll',
    r'D3DX_MODULE C:\X3\d3dx9_37.dll',
    'ATTACH result=00000000 enabled=1',
    'ADAPTER_DESC a later row is ignored',
])


class NativeHost(unittest.TestCase):
    def test_detection(self):
        with tempfile.TemporaryDirectory() as tmp:
            interop = Path(tmp) / 'WSLInterop'
            self.assertEqual(len(native_host.missing(interop, lambda name: None)), 2)
            interop.write_text('enabled')
            self.assertEqual(native_host.missing(interop, lambda name: '/usr/bin/' + name), [])
            self.assertTrue(native_host.available(interop, lambda name: '/usr/bin/' + name))
            self.assertFalse(native_host.available(interop, lambda name: None))

    def test_results_dir(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = native_host.results_dir(Path(tmp), 'engine-plumes')
            self.assertEqual(path, Path(tmp) / 'verification/results/native-windows/engine-plumes')
            self.assertTrue(path.is_dir())

    def test_tasklist_and_ver(self):
        listing = '"System","4","Services","0","10 K"\r\n"x3ap.exe","5512","Console","1","2 K"\r\n"X3APx.exe","9","Console","1","1 K"\r\n'
        self.assertEqual(native_host.parse_tasklist(listing), ['x3ap.exe'])
        self.assertEqual(native_host.parse_tasklist('"System","4","Services","0","10 K"'), [])
        self.assertEqual(native_host.parse_ver('\r\nMicrosoft Windows [Version 10.0.26300.9818]\r\n'), '10.0.26300.9818')
        self.assertIsNone(native_host.parse_ver('no version'))

    def test_pe_file_version(self):
        self.assertEqual(native_host.pe_file_version(fake_pe()), '10.0.26100.1')
        self.assertIsNone(native_host.pe_file_version(b'MZ' + b'\0' * 100))

    def test_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            d3d9, d3dx = Path(tmp) / 'd3d9.dll', Path(tmp) / 'd3dx9_37.dll'
            d3d9.write_bytes(fake_pe())
            d3dx.write_bytes(fake_pe(9, 15, 779, 0))
            paths = {r'C:\Windows\SYSTEM32\d3d9.dll': d3d9, r'C:\X3\d3dx9_37.dll': d3dx}
            block = native_host.identity(STDOUT, version={'version': '10.0.26300.9818'}, to_linux=lambda w: str(paths[w]))
        self.assertEqual(block['name'], 'native-windows')
        self.assertEqual(block['windows']['version'], '10.0.26300.9818')
        a = block['adapter']
        self.assertEqual((a['description'], a['driver'], a['vendor'], a['device'], a['driver_version']),
                         ('NVIDIA GeForce RTX 4090', 'nvldumd.dll', '0x10de', '0x2684', '32.0.15.6094'))
        self.assertEqual(block['d3d9_dll']['path'], r'C:\Windows\SYSTEM32\d3d9.dll')
        self.assertEqual(block['d3d9_dll']['sha256'], hashlib.sha256(fake_pe()).hexdigest())
        self.assertEqual(block['d3d9_dll']['file_version'], '10.0.26100.1')
        self.assertEqual(block['d3dx9_37_dll']['file_version'], '9.15.779.0')
        # No identity rows (an older binary or a failed Direct3DCreate9): the block says so instead of guessing.
        empty = native_host.identity('RESULT FAIL', version={'version': None})
        self.assertIsNone(empty['adapter']['description'])
        self.assertIsNone(empty['d3d9_dll'])
        missing = native_host.module_provenance(r'C:\nope.dll', to_linux=lambda w: '/nonexistent/nope.dll')
        self.assertIn('error', missing)

    def test_wslenv_forwarding(self):
        env = native_host.child_environment({'X3M_PLUMES_FIXTURE_DISC_AB': '1'}, {'WSLENV': 'USERPROFILE/p:X3M_PLUMES_FIXTURE_DISC_AB'})
        self.assertEqual(env['WSLENV'], 'USERPROFILE/p:X3M_PLUMES_FIXTURE_DISC_AB')
        env = native_host.child_environment({'A': '1', 'B': '2'}, {})
        self.assertEqual((env['WSLENV'], env['A']), ('A:B', '1'))
        self.assertNotIn('WSLENV', native_host.child_environment({}, {}))

    def test_ownership_default_and_override(self):
        # X3M_OWNERSHIP=1 reaches the Windows process unless the runner sets it; an explicit value (0 included) is kept.
        seen = []

        def fake_run(command, **kw):
            seen.append(kw['env'])
            return subprocess.CompletedProcess(command, 0, b'', b'')
        with mock.patch.object(native_host.subprocess, 'run', side_effect=fake_run), \
                mock.patch.dict(native_host.os.environ, {'WSLENV': ''}):
            native_host.run_native(Path('/tmp/fixture.exe'), [], '/tmp', 1)
            native_host.run_native(Path('/tmp/fixture.exe'), [], '/tmp', 1, env={'X3M_OWNERSHIP': '0', 'X3M_DEBUG': '1'})
            native_host.run_native(Path('/tmp/fixture.exe'), [], '/tmp', 1, env={'X3M_DEBUG': '1'})
        self.assertEqual([e['X3M_OWNERSHIP'] for e in seen], ['1', '0', '1'])
        for e in seen:
            self.assertIn('X3M_OWNERSHIP', e['WSLENV'].split(':'))
        self.assertEqual(native_host.fixture_environment({}), {'X3M_OWNERSHIP': '1'})

    def test_refuses_a_game_image(self):
        for name in ('X3AP.exe', 'x3fl.EXE'):
            with self.assertRaises(SystemExit):
                native_host.run_native(Path('/mnt/c/X3') / name, [], '/tmp', 1)


if __name__ == '__main__':
    unittest.main()
