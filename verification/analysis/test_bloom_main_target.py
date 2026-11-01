"""Host test of the bloom main-target admission (src/renderer/bloom_main_target.h, used by
BloomPass::validate_inputs clause 7 and validate_boundary): native D3D9 reports the game's main
render target as a texture level (Type D3DRTYPE_TEXTURE, Usage 0x800001), Wine as a surface; both
are admitted, every other resource type and the remaining conditions are refused. No game, no Wine."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "bloom_main_target.h"
#include <cstdio>
struct Desc { unsigned Format, Type, Usage, Pool, MultiSampleType; unsigned long MultiSampleQuality; unsigned Width, Height; };
int main() {
    // Native row: fmt 0x15, type 3, usage 0x800001, pool 0, no MSAA, 5120x1440.
    const Desc native{0x15, 3, 0x800001, 0, 0, 0, 5120, 1440};
    Desc wine = native; wine.Type = 1; wine.Usage = 1;
    Desc volume = native; volume.Type = 4;
    Desc cube = native; cube.Type = 5;
    Desc no_rt = native; no_rt.Usage = 0x800000;
    Desc managed = native; managed.Pool = 1;
    Desc msaa = native; msaa.MultiSampleType = 4;
    Desc format = native; format.Format = 0x16;
    Desc empty = native; empty.Width = 0;
    using x3m::renderer::bloom_main_descriptor;
    std::printf("native=%d wine=%d volume=%d cube=%d no_rt=%d managed=%d msaa=%d format=%d empty=%d\n",
                bloom_main_descriptor(native), bloom_main_descriptor(wine), bloom_main_descriptor(volume),
                bloom_main_descriptor(cube), bloom_main_descriptor(no_rt), bloom_main_descriptor(managed),
                bloom_main_descriptor(msaa), bloom_main_descriptor(format), bloom_main_descriptor(empty));
}
'''


class BloomMainTarget(unittest.TestCase):
    def test_texture_level_and_surface_admitted_other_types_refused(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('no C++ compiler on the host')
        with tempfile.TemporaryDirectory(prefix='x3-bloom-main-') as directory:
            source, exe = Path(directory) / 'harness.cpp', Path(directory) / 'harness'
            source.write_text(HARNESS)
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/renderer'),
                                    str(source), '-o', str(exe)], capture_output=True, text=True, timeout=120)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stderr)
        fields = dict(item.split('=') for item in run.stdout.split())
        self.assertEqual(fields, {'native': '1', 'wine': '1', 'volume': '0', 'cube': '0', 'no_rt': '0', 'managed': '0',
                                  'msaa': '0', 'format': '0', 'empty': '0'})

    def test_bloom_pass_routes_through_the_shared_rule(self):
        text = (ROOT / 'src/renderer/bloom_pass.cpp').read_text()
        self.assertIn('#include "bloom_main_target.h"', text)
        self.assertIn('{ return bloom_main_descriptor(d); }', text)
        self.assertNotIn('d.Type == D3DRTYPE_SURFACE', text)


if __name__ == '__main__':
    unittest.main()
