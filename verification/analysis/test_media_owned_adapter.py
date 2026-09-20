"""Compile actual owned-media state sources. No mirrored model, game or Wine."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [ROOT / 'src/media/playback_runtime.cpp', ROOT / 'src/proxy/media_playback.cpp']
FIXTURE = ROOT / 'verification/probe/media_owned_adapter_state_fixture.cpp'


class OwnedAdapter(unittest.TestCase):
    def test_production_state_host(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-owned-state-') as directory:
            exe = Path(directory) / 'state'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    *map(str, SOURCES), str(FIXTURE), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'checks=\d+ failures=0 allocations=0 iterations=200000 pair_ns=')
            print(run.stdout.strip())

    def test_native_windows_cross_compile_and_no_x87(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        objdump = shutil.which('i686-w64-mingw32-objdump')
        self.assertIsNotNone(compiler)
        self.assertIsNotNone(objdump)
        with tempfile.TemporaryDirectory(prefix='x3-owned-state-x86-') as directory:
            for source in SOURCES:
                obj = Path(directory) / (source.stem + '.o')
                build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                        '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                                        '-fno-exceptions', '-c', str(source), '-o', str(obj)], capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                dump = subprocess.run([objdump, '-d', '--no-show-raw-insn', '-Mintel', str(obj)], capture_output=True, text=True, check=True).stdout
                instructions = re.findall(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)', dump, re.M)
                x87 = [name for name in instructions if name.startswith('f') and name not in ('fs',)]
                self.assertEqual(x87, [], f'{source}: x87 instructions {x87}')


if __name__ == '__main__':
    unittest.main()
