"""The ambient occlusion pass's capability decision and slot count on the host.

Compiles src/renderer/ambient_occlusion_caps.h (no d3d9.h) with the native
compiler and drives the pure functions: the gate order and reasons, the
conservative ps_3_0 slot count of a synthetic program and of the embedded
GTAO program (must fit the 512-slot minimum). No Wine, no device.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "ambient_occlusion_caps.h"
#include <cstdio>
using namespace x3m::renderer;
int main() {
    AmbientOcclusionCapabilityInputs ok{0xffff0300u, 0xfffe0300u, 512, 504, 4, 16, 0, 0, 0, true, true};
    auto reason = [](AmbientOcclusionCapabilityInputs in) { const char* r = ambient_occlusion_capability(in); return r ? r : "ok"; };
    std::printf("all=%s\n", reason(ok));
    auto v = ok; v.pixel_shader_version = 0xffff0200u; std::printf("ps=%s\n", reason(v));
    v = ok; v.vertex_shader_version = 0xfffe0200u; std::printf("vs=%s\n", reason(v));
    v = ok; v.ps30_instruction_slots = 400; std::printf("slots=%s\n", reason(v));
    v = ok; v.largest_program_slots = 0; std::printf("malformed=%s\n", reason(v));
    v = ok; v.simultaneous_targets = 0; std::printf("caps=%s\n", reason(v));
    v = ok; v.r32f_target = -1; std::printf("r32f=%s\n", reason(v));
    v = ok; v.r16f_target = -1; std::printf("r16f=%s\n", reason(v));
    v = ok; v.target_blending = -1; std::printf("blend=%s\n", reason(v));
    v = ok; v.dest_blend_srccolor = false; std::printf("factors=%s\n", reason(v));
    // ps_3_0, dcl (0), def (0), texld (1), sincos (8), rep (3), endrep (1), mov (1), end: 14 slots.
    const std::uint32_t words[] = {0xffff0300u, 0x0200001fu, 0x90000000u, 0xa00f0800u, 0x05000051u, 0xa00f0000u, 0, 0, 0, 0,
                                   0x03000042u, 0x800f0000u, 0x90e40000u, 0xa0e40800u, 0x02000025u, 0x80030000u, 0x80000000u,
                                   0x01000026u, 0xf0e40000u, 0x00000027u, 0x02000001u, 0x800f0800u, 0x80e40000u, 0x0000ffffu};
    std::printf("synthetic=%u\n", ambient_occlusion_program_slots(words, sizeof words / sizeof words[0]));
    std::printf("truncated=%u\n", ambient_occlusion_program_slots(words, 5));
    const std::uint32_t gtao[] = {
#include "ambient_occlusion_gtao_program_inc.h"
    };
    std::printf("gtao=%u\n", ambient_occlusion_program_slots(gtao, sizeof gtao / sizeof gtao[0]));
    return 0;
}
'''


class AmbientOcclusionCapsTests(unittest.TestCase):
    def test_gates_and_slots(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-ao-caps-') as temporary:
            source, executable = Path(temporary) / 'driver.cpp', Path(temporary) / 'driver'
            source.write_text(DRIVER)
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/renderer'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
        values = dict(line.split('=', 1) for line in run.stdout.splitlines())
        self.assertEqual({k: values[k] for k in ('all', 'ps', 'vs', 'slots', 'malformed', 'caps', 'r32f', 'r16f', 'blend', 'factors')},
                         {'all': 'ok', 'ps': 'ps_3_0', 'vs': 'vs_3_0', 'slots': 'ps_slots', 'malformed': 'ps_slots', 'caps': 'device_caps',
                          'r32f': 'r32f_target', 'r16f': 'r16f_target', 'blend': 'target_blending', 'factors': 'blend_factors'})
        self.assertEqual((values['synthetic'], values['truncated']), ('14', '0'))
        gtao = int(values['gtao'])
        self.assertTrue(0 < gtao <= 512, gtao)
        # The provenance record's word count matches the embedded header.
        header = (ROOT / 'src/renderer/ambient_occlusion_gtao_program_inc.h').read_text()
        self.assertIn('"word_count": %d' % len(re.findall(r'0x[0-9a-f]{8}u', header)),
                      (ROOT / 'verification/results/ambient-occlusion-gtao-program.json').read_text())


if __name__ == '__main__':
    unittest.main()
