"""Host execution of the production meter parser, including stale oversized reads."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ExposureSettings(unittest.TestCase):
    def test_complete_finite_bounded_parameters(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        source = r'''
#include "exposure.h"
#include <cwchar>
#include <string>
#include <cstdio>
using x3m::renderer::parse_meter_parameter;
int main() {
    unsigned checks = 0;
    auto reject = [&](const wchar_t* text, std::size_t length, float low=0.f, float high=4.f) {
        float output = .9f;
        if (parse_meter_parameter(text, length, low, high, output) || output != .9f) return false;
        ++checks; return true;
    };
    for (const auto text : {L"", L"garbage", L"0junk", L"nan", L"NaN", L"inf", L"-inf", L"1e999", L"-1", L"4.1", L"0 "})
        if (!reject(text, std::wcslen(text))) return 1;
    // A failed/oversized Windows getter may leave a previous short value in
    // its destination. The returned required length must be checked first.
    const wchar_t stale[2] = {L'0', L'\0'};
    if (!reject(stale, 32) || !reject(stale, 999999) || !reject(nullptr, 1)) return 2;
    if (!reject(L"0x", 1) || !reject(L"0", 1, 1e-4f, 64.f)) return 3;
    for (const auto text : {L"0", L"0.25", L"4", L"1e-1"}) {
        float output = .9f;
        if (!parse_meter_parameter(text, std::wcslen(text), 0.f, 4.f, output) || output != std::wcstof(text, nullptr)) return 4;
        ++checks;
    }
    std::wstring boundary(31, L'0'); float output = .9f;
    if (!parse_meter_parameter(boundary.c_str(), boundary.size(), 0.f, 4.f, output) || output != 0.f) return 5;
    ++checks;
    std::printf("PASS checks=%u\n", checks);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            fixture = directory / 'settings.cpp'
            fixture.write_text(source)
            for mode, flags in [('release', ['-O2']), ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined'])]:
                with self.subTest(mode=mode):
                    executable = directory / mode
                    subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                                    '-I', str(ROOT / 'src/renderer'), str(fixture),
                                    str(ROOT / 'src/renderer/exposure.cpp'), '-o', str(executable)],
                                   check=True, capture_output=True, text=True, timeout=60)
                    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, 'PASS checks=21\n')
                    self.assertEqual(result.stderr, '')


if __name__ == '__main__':
    unittest.main()
