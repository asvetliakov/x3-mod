"""Execute the fixture's real assertion helpers without a device or Wine."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class MotionFixtureLoggingTests(unittest.TestCase):
    def test_quiet_success_counts_and_first_failure_match_verbose_helper(self):
        source = (ROOT / 'verification/probe/motion_output_fixture.cpp').read_text()
        helpers = '\n'.join(extract_function(source, name) for name in (
            'void require(bool ok, const char* label)',
            'void require_quiet(bool ok, const char* label)'))
        program = '''#include <cstdio>
#include <stdexcept>
#include <cstring>
unsigned checks = 0;
''' + helpers + '''
int main() {
    for (unsigned i = 0; i != 100000; ++i) require_quiet(true, "pixel");
    if (checks != 100000) return 1;
    for (unsigned quiet = 0; quiet != 2; ++quiet) {
        checks = 0;
        try {
            if (quiet) require_quiet(false, "first pixel failure");
            else require(false, "first pixel failure");
            return 2;
        } catch (const std::runtime_error& e) {
            if (checks != 1 || std::strcmp(e.what(), "first pixel failure")) return 3;
        }
    }
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            cpp, exe = Path(temp) / 'checks.cpp', Path(temp) / 'checks'
            cpp.write_text(program)
            build = subprocess.run(['c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        self.assertEqual(result.stdout, 'CHECK first pixel failure FAIL\n' * 2)
        self.assertEqual(result.stderr, '')


if __name__ == '__main__':
    unittest.main()
