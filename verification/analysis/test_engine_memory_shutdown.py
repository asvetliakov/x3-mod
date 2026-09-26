"""Host-test the engine memory reader's cache trust, stall bound and shutdown signal.

Compiles the production src/proxy/engine_memory.cpp on the host against a mock
<windows.h> (VirtualQuery over a synthetic region table, a settable tick,
LastError) with its `rep movsb` replaced by a counted copy, and runs
verification/probe/engine_memory_host.cpp: the Run 77 exit shape (a region
validated at the last Present, frames stopped, the block decommitted 0.9 s
later) must be refused, the hot path must issue one VirtualQuery per distinct
region per frame, a stall must trust a region for 5 ms after its validation,
revalidate() must neither restart the stall bound nor end the shutdown signal,
and the shutdown signal must force a query per read. The x86
build, its no-SSE contract and Wine behaviour are the object-lifetime fixture's.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]
COPY = '__asm__ __volatile__("rep movsb" : "+D"(out), "+S"(in), "+c"(size) : : "memory");'
MOCK_WINDOWS = '''#pragma once
#include <cstddef>
#include <cstdint>
typedef unsigned long DWORD;
typedef std::size_t SIZE_T;
struct MEMORY_BASIC_INFORMATION {
    void* BaseAddress; void* AllocationBase; DWORD AllocationProtect; SIZE_T RegionSize; DWORD State, Protect, Type;
};
constexpr DWORD MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000;
constexpr DWORD PAGE_NOACCESS = 0x01, PAGE_READONLY = 0x02, PAGE_READWRITE = 0x04, PAGE_WRITECOPY = 0x08,
    PAGE_EXECUTE_READ = 0x20, PAGE_EXECUTE_READWRITE = 0x40, PAGE_EXECUTE_WRITECOPY = 0x80, PAGE_GUARD = 0x100;
SIZE_T VirtualQuery(const void* address, MEMORY_BASIC_INFORMATION* info, SIZE_T length);
DWORD GetTickCount();
DWORD GetLastError();
void SetLastError(DWORD value);
void host_copy(void* out, const void* in, std::size_t size);
'''


def build_and_run(test, source_text, extra_flags=()):
    compiler = shutil.which('clang++') or shutil.which('c++')
    test.assertIsNotNone(compiler, 'A host C++ compiler is required')
    test.assertEqual(source_text.count(COPY), 1, 'copy_bytes anchor moved; update the host substitution')
    with tempfile.TemporaryDirectory(prefix='x3-engine-memory-host-') as temporary:
        directory = Path(temporary)
        (directory / 'windows.h').write_text(MOCK_WINDOWS)
        (directory / 'engine_memory_under_test_inc.h').write_text(
            source_text.replace(COPY, 'host_copy(out, in, size);'))
        executable = directory / 'engine_memory_host'
        build = subprocess.run([
            compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', *extra_flags,
            '-I', str(directory), '-I', str(ROOT / 'src/proxy'),
            str(ROOT / 'verification/probe/engine_memory_host.cpp'), '-o', str(executable),
        ], capture_output=True, text=True)
        test.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        return subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)


class EngineMemoryShutdownTests(unittest.TestCase):
    def test_stale_cache_hot_path_and_shutdown_signal(self):
        run = build_and_run(self, source_text(ROOT / 'src/proxy/engine_memory.cpp'))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual(run.stderr, '')
        lines = run.stdout.splitlines()
        self.assertEqual(lines[-1], 'engine_memory_host scenarios=7 checks=74 failures=0')
        hot = re.fullmatch(r'HOTPATH frames=1000 reads=48000 queries=(\d+) ticks=(\d+) host_ns_per_read=[\d.]+', lines[-2])
        self.assertIsNotNone(hot, lines[-2])
        # One query per distinct region per frame; one tick per read plus one per frame advance.
        self.assertEqual(int(hot[1]), 3000)
        self.assertEqual(int(hot[2]), 48000 + 1000)

    def test_signal_raised_by_registry_destroy_and_summarized_once(self):
        lifetime = source_text(ROOT / 'src/proxy/object_lifetime.cpp')
        self.assertEqual(lifetime.count('x3m::engine_memory::begin_shutdown('), 1)
        destroy = lifetime[lifetime.index('if(scope->kind==Destroy){'):]
        self.assertIn('begin_shutdown("registry_destroy")', destroy[:300])  # the Destroy branch (300 raw characters after formatting)
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        self.assertEqual(capture.count('engine_memory_read_refused reason='), 1)
        self.assertEqual(capture.count('engine_memory_refused_line();'), 1)
        self.assertIn('last_device_destroyed=!refs&&devices.empty();\n        if(last_device_destroyed)engine_memory_refused_line();',
                      capture)


if __name__ == '__main__':
    unittest.main()
