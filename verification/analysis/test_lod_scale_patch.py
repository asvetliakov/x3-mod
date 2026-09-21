"""Host checks of the LOD threshold scale patch (src/proxy/lod_scale_core.h).

The byte window constant against the disassembled site, the same-length
replacement encoding, the fail-closed mirror rule (compiled from the core
header with the host compiler) and the `lod_scale` log-line parser. Read-only;
the installed executable is only read when present.
"""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_lod_scale_site as probe  # noqa: E402

HARNESS = r'''
#include "lod_scale_core.h"
#include <cstdio>
using namespace x3m::lod_scale::core;
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    unsigned char code[6]; encode_replacement(0x1234abcdu, code);
    const unsigned char want[6] = {0xd8, 0x0d, 0xcd, 0xab, 0x34, 0x12};
    check(std::memcmp(code, want, 6) == 0, "encoding");
    check(sizeof expected_window == 17 && sizeof expected_site == 6 && std::memcmp(expected_window + 11, expected_site, 6) == 0, "window_tail_is_site");
    check(site_va - window_va == 11 && next_va - site_va == site_length, "boundaries");
    check(valid_factor(1.0) && valid_factor(4.0) && valid_factor(2.5), "factor_band");
    check(!valid_factor(0.999) && !valid_factor(4.001) && !valid_factor(0.0) && !valid_factor(-2.0), "factor_out_of_band");
    check(!valid_factor(__builtin_nan("")) && !valid_factor(__builtin_inf()), "factor_not_finite");
    std::uint32_t out = 0;
    check(mirror_bits(float_to_bits(1.0f), 2.0, &out) && bits_to_float(out) == 0.5f, "scale_1_0");
    check(mirror_bits(float_to_bits(1.4f), 4.0, &out) && bits_to_float(out) == 0.35f, "scale_1_4_at_cap");
    check(mirror_bits(float_to_bits(1.15f), 1.0, &out) && out == float_to_bits(1.15f), "factor_one_identity");
    const std::uint32_t zero = 0, big = float_to_bits(1.5f), small = float_to_bits(0.5f), nanbits = 0x7fc00000u, infbits = 0x7f800000u, neg = float_to_bits(-1.0f);
    const std::uint32_t cases[] = {zero, big, small, nanbits, infbits, neg};
    for (std::uint32_t bits : cases) {
        check(!mirror_bits(bits, 2.0, &out) && out == bits, "passthrough_out_of_band");
    }
    check(!mirror_bits(float_to_bits(1.0f), 8.0, &out) && out == float_to_bits(1.0f), "passthrough_bad_factor");
    double parsed = 0;
    check(parse_factor("2", &parsed) && parsed == 2.0, "parse_integer");
    check(parse_factor("2.5", &parsed) && parsed == 2.5, "parse_decimal");
    check(parse_factor("+1.25", &parsed) && parsed == 1.25, "parse_plus");
    check(parse_factor(".5", &parsed) && parsed == 0.5, "parse_leading_point");
    check(parse_factor("4.", &parsed) && parsed == 4.0, "parse_trailing_point");
    const char* bad[] = {"", "2,5", " 2", "2 ", "2e0", "-2", "two", ".", "+", "0x2", "nan", "inf"};
    for (const char* text : bad) check(!parse_factor(text, &parsed), "parse_rejects");
    check(!parse_factor(nullptr, &parsed), "parse_null");
    std::printf("lod_scale_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


class LodScalePatch(unittest.TestCase):
    def test_window_constant_matches_site_bytes(self):
        core = probe.source_constants(probe.CORE.read_text())
        self.assertEqual(core['window'], probe.WINDOW)
        self.assertEqual((core['window_va'], core['site_va'], core['next_va']), (0x47d440, 0x47d44b, 0x47d451))
        self.assertEqual(core['window'][11:], bytes.fromhex('d88960070000'))  # fmul dword [ecx+0x760]
        self.assertEqual(core['window'][5:11], bytes.fromhex('8b0d346f6000'))  # mov ecx,[0x606f34]
        self.assertEqual((core['config_pointer_va'], core['config_scale_offset']), (0x606f34, 0x760))

    def test_replacement_encoding(self):
        code = probe.encode_replacement(0x10203040)
        self.assertEqual(len(code), 6)
        self.assertEqual(code[0], 0xd8)                       # FMUL m32fp opcode
        self.assertEqual((code[1] >> 6, (code[1] >> 3) & 7, code[1] & 7), (0, 1, 5))  # mod=00, /1, r/m=101 (disp32)
        self.assertEqual(struct.unpack('<I', code[2:])[0], 0x10203040)
        self.assertEqual(code[2:], bytes([0x40, 0x30, 0x20, 0x10]))
        with self.assertRaises(ValueError):
            probe.encode_replacement(1 << 32)

    def test_core_rules_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-lod-scale-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'lod_scale_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'lod_scale_core checks_failed=0\n')

    def test_log_line_parser(self):
        row = probe.parse_log_line('00:00:01.234 lod_scale requested=2 applied=0 game_value=0 proxy_value=0 patched=1 reason=game_value_pending write=plain')
        self.assertEqual(row, {'requested': 2.0, 'applied': 0.0, 'game_value': 0.0, 'proxy_value': 0.0, 'patched': True, 'reason': 'game_value_pending', 'write': 'plain'})
        row = probe.parse_log_line('lod_scale requested=2 applied=2 game_value=1 proxy_value=0.5 patched=1 reason=ok write=plain')
        self.assertEqual((row['applied'], row['proxy_value'], row['patched'], row['reason']), (2.0, 0.5, True, 'ok'))
        row = probe.parse_log_line('lod_scale requested=5 applied=0 game_value=0 proxy_value=0 patched=0 reason=invalid_factor write=none')
        self.assertEqual((row['patched'], row['reason'], row['write']), (False, 'invalid_factor', 'none'))
        row = probe.parse_log_line('lod_scale requested=2,5 applied=0 game_value=0 proxy_value=0 patched=0 reason=invalid_factor write=none')
        self.assertEqual((row['requested'], row['patched']), ('2,5', False))
        self.assertIsNone(probe.parse_log_line('lod_scale requested=2 applied=2 game_value=1 proxy_value=0.5 patched=1 reason=ok'))  # no write field
        self.assertIsNone(probe.parse_log_line('lod_scale_value game_value=1 proxy_value=0.5 applied=2'))

    def test_synthetic_image_refuses_changed_window(self):
        from test_chase_aim_sites import synthetic_image
        for change, expect in (((), True), (((probe.SITE_VA + 2, b'\x64'),), False), (((probe.WINDOW_VA + 1, b'\x0b'),), False)):
            data = synthetic_image(extra=((probe.WINDOW_VA, probe.WINDOW), (probe.NEXT_VA, b'\xe8' + struct.pack('<i', probe.FTOL_VA - (probe.NEXT_VA + 5))), *change))
            report = probe.verify(data)
            checks = {k: v for k, v in report['checks'].items() if k != 'exe_identity'}
            self.assertEqual(all(checks.values()), expect, report)
            # The wrong-hash gate rejects a synthetic image even with the bytes in place.
            self.assertFalse(report['checks']['exe_identity'])
            self.assertEqual(report['result'], 'FAIL')

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)


if __name__ == '__main__':
    unittest.main()
