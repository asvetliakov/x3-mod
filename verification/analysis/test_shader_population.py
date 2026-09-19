"""Host unit test of the shader-population classifier (src/renderer/shader_population.*).

The production classifier and every translation unit that feeds it a table are
compiled natively (no Wine, no D3D, no game bytes) into
verification/probe/shader_population_host.cpp, which answers line commands.
The checks are: every entry of every consulted table is recognised, hashes the
tables do not contain are not, the fixed 256-entry session table dedupes and
counts its overflow, and the proxy's create/Present wiring emits the documented
schema only with telemetry on.
"""
from pathlib import Path
import random
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'verification/probe/shader_population_host.cpp'
SOURCES = [
    'src/renderer/shader_population.cpp',
    'src/renderer/linear_material.cpp',
    'src/renderer/linear_emission.cpp',
    'src/renderer/linear_emission_sm1.cpp',
    'src/renderer/rigid_position.cpp',
    'src/renderer/material_radiance.cpp',
    'src/renderer/material_motion.cpp',
]


class ShaderPopulationHost:
    """One compiled driver process, reused by every test in the class."""

    def __init__(self, directory):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if compiler is None:
            raise unittest.SkipTest('no host C++ compiler')
        self.exe = Path(directory) / 'shader_population_host'
        subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'src/renderer'), str(DRIVER)]
                       + [str(ROOT / s) for s in SOURCES] + ['-o', str(self.exe)], check=True)

    def run(self, script):
        out = subprocess.run([str(self.exe)], input=script, capture_output=True, text=True)
        if out.returncode != 0:
            raise AssertionError('driver failed: %s %s' % (out.returncode, out.stderr))
        return out.stdout.splitlines()


class ShaderPopulationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.host = ShaderPopulationHost(cls.directory.name)
        cls.tables = [line.split() for line in cls.host.run('tables\n') if line.startswith('table ')]
        cls.entries = [line.split() for line in cls.host.run('dump\n')]

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_the_consulted_set_covers_every_arm_the_proxy_keys_on(self):
        names = [name for _, name, _ in self.tables]
        # One table per keyed arm; a new arm must be added here deliberately.
        self.assertEqual(names, [
            'motion_output_pairs', 'depth_prepass_vertex', 'scene_boundary_bloom_pairs',
            'fade_route_vertex', 'cutout_pairs', 'screen_emission_pairs',
            'linear_material_vertex', 'linear_material_pixel', 'linear_material_pairs',
            'linear_material_palette', 'material_exposure_seed', 'linear_xt_pixel',
            'linear_xt_vertex', 'linear_distance_fade_pairs', 'linear_emission_pixel',
            'linear_emission_pairs', 'linear_emission_sm1_pixel', 'linear_emission_sm1_pairs',
            'rigid_position_vertex', 'position_path_vertex', 'pixel_coverage_pixel',
            'material_radiance_pixel'])
        summary = self.host.run('tables\n')[-1].split()
        self.assertEqual(int(summary[1]), len(names))
        self.assertEqual(int(summary[3]), sum(int(count) for _, _, count in self.tables))
        self.assertEqual(len(self.entries), int(summary[3]))
        # The twenty linear-emission pairs, the nine bullet pairs and the four
        # depth-only aliases are the named sets of mod-compatibility.md.
        counts = {name: int(count) for _, name, count in self.tables}
        self.assertEqual(counts['linear_emission_pairs'], 20 * 2)
        self.assertEqual(counts['linear_emission_sm1_pairs'], 9 * 2)
        self.assertEqual(counts['depth_prepass_vertex'], 4)  # z_only and the base z_only_0000/0001 copies
        self.assertEqual(counts['linear_material_pairs'] % 2, 0)
        self.assertEqual(counts['linear_material_pairs'] // 2, 154)

    def test_every_table_entry_is_known(self):
        hashes = sorted({row[2] for row in self.entries})
        self.assertGreater(len(hashes), 400)
        answers = self.host.run(''.join('known %s\n' % h for h in hashes))
        self.assertEqual(len(answers), len(hashes))
        unrecognised = [h for h, a in zip(hashes, answers) if a != 'known 1']
        self.assertEqual(unrecognised, [])

    def test_random_hashes_and_the_null_program_are_unknown(self):
        table = {row[2] for row in self.entries}
        rng = random.Random(20260916)
        probes = []
        while len(probes) < 200:
            candidate = '%016x' % rng.getrandbits(64)
            if candidate not in table:
                probes.append(candidate)
        probes.append('0000000000000000')
        answers = self.host.run(''.join('known %s\n' % h for h in probes))
        self.assertEqual(set(answers), {'known 0'})

    def test_a_flipped_bit_of_a_known_program_is_unknown(self):
        table = {row[2] for row in self.entries}
        probes = ['%016x' % (int(h, 16) ^ 1) for h in sorted(table)[:64]]
        probes = [h for h in probes if h not in table]
        answers = self.host.run(''.join('known %s\n' % h for h in probes))
        self.assertEqual(set(answers), {'known 0'})

    def test_unknown_programs_are_recorded_once_and_drained_in_order(self):
        known = self.entries[0][2]
        script = ('observe %s ps ffff0300 1024\n' % known
                  + 'observe aaaaaaaaaaaaaaa1 ps ffff0200 512\n'
                  + 'observe aaaaaaaaaaaaaaa1 ps ffff0200 512\n'
                  + 'observe aaaaaaaaaaaaaaa2 vs fffe0101 356\n'
                  + 'observe 0000000000000000 vs fffe0101 356\n'
                  + 'take\ntake\ntake\n')
        lines = self.host.run(script)
        self.assertEqual(lines[0], 'observe 0 1 0 0')   # a known program only moves known
        self.assertEqual(lines[1], 'observe 1 1 1 0')   # first sight of an unknown
        self.assertEqual(lines[2], 'observe 0 1 1 0')   # repeat: no second line, no count
        self.assertEqual(lines[3], 'observe 1 1 2 0')
        self.assertEqual(lines[4], 'observe 0 1 2 0')   # hash 0 is never a program
        self.assertEqual(lines[5], 'take 1 aaaaaaaaaaaaaaa1 %u 512 ps' % 0xffff0200)
        self.assertEqual(lines[6], 'take 1 aaaaaaaaaaaaaaa2 %u 356 vs' % 0xfffe0101)
        self.assertEqual(lines[7], 'take 0')

    def test_the_fixed_table_holds_256_and_counts_the_overflow(self):
        script = ''.join('observe %016x vs fffe0300 128\n' % (0xb000000000000000 + i)
                         for i in range(260))
        lines = self.host.run(script)
        self.assertEqual(lines[255], 'observe 1 0 256 0')
        self.assertEqual(lines[256], 'observe 0 0 256 1')
        self.assertEqual(lines[259], 'observe 0 0 256 4')
        drained = self.host.run(script + 'take\n' * 257)
        self.assertEqual(sum(1 for line in drained if line.startswith('take 1')), 256)
        self.assertEqual(drained[-1], 'take 0')

    def test_counts_changed_reports_once_per_movement(self):
        lines = self.host.run('changed\nobserve aaaaaaaaaaaaaaa1 ps ffff0200 8\n'
                              'changed\nchanged\n'
                              'observe aaaaaaaaaaaaaaa1 ps ffff0200 8\nchanged\n')
        self.assertEqual([lines[0], lines[2], lines[3], lines[5]],
                         ['changed 0', 'changed 1', 'changed 0', 'changed 0'])

    def test_every_enumerated_distance_fade_pair_still_routes(self):
        # The classifier enumerates the same rows the sampler-mask lookup
        # scans, so this covers the station pair too (it is named, not literal,
        # in the source).
        index = [name for _, name, _ in self.tables].index('linear_distance_fade_pairs')
        hashes = [row[2] for row in self.entries if int(row[1]) == index]
        self.assertEqual(len(hashes), 14)
        pairs = [(hashes[i], hashes[i + 1]) for i in range(0, len(hashes), 2)]
        masks = [line.split()[1] for line in self.host.run(''.join('fade %s %s\n' % p for p in pairs))]
        self.assertEqual(len(masks), 7)
        self.assertNotIn('0', masks, dict(zip(pairs, masks)))
        self.assertEqual(masks, ['7', '7', '7', '15', '15', '15', '31'])
        # Swapping the two sides of a row must not route: the lookup keys on both.
        swapped = [line.split()[1] for line in
                   self.host.run(''.join('fade %s %s\n' % (ps, vs) for vs, ps in pairs))]
        self.assertEqual(set(swapped), {'0'})

    def test_each_arm_predicate_is_derived_from_the_table_it_enumerates(self):
        # A hash added to one form only would otherwise keep both asserts happy.
        fade_route = (ROOT / 'src/proxy/fade_route_core.h').read_text()
        self.assertIn('for (const auto& row : vertex_programs)', fade_route)
        self.assertNotIn('switch (vs)', fade_route)
        cutout = (ROOT / 'src/proxy/linear_cutout.h').read_text()
        self.assertIn('for (std::size_t i = 0; i + 1 < pair_hash_count; i += 2)', cutout)
        material = (ROOT / 'src/renderer/linear_material.cpp').read_text()
        self.assertIn('for (const auto& row : distance_fade_rows)', material)
        self.assertNotIn('case station_fade_vs:', material)

    def test_the_proxy_classifies_at_create_and_logs_at_present(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        # Create path: once per distinct program, telemetry only, no per-draw work.
        self.assertIn('if (dumped.insert(hash).second) {', capture)
        self.assertIn("shader_population.observe(hash, kind[0]=='v', version, bytes);", capture)
        classify = capture.split('if (dumped.insert(hash).second) {')[1].split('return hash;')[0]
        self.assertIn('if (telemetry::enabled()) {', classify)
        # Present path: the two documented lines, both behind telemetry.
        present = capture.split('HRESULT WINAPI present(')[1]
        self.assertIn('log("shader_unknown kind=%s id=%016llx version=%08lx bytes=%lu tables=%lu"', capture)
        self.assertIn('log("shader_population known=%lu unknown=%lu overflow=%lu"', capture)
        self.assertIn('void report_shader_population(bool population) {', capture)
        self.assertIn('if(!telemetry::enabled())return;', capture)
        self.assertIn('if(population&&shader_population.counts_changed())', capture)
        self.assertIn('report_shader_population(ctx.frame%300==0);', present)
        self.assertNotIn('shader_population.observe', present)
        # Session end: the last count movement is flushed when the device goes.
        self.assertIn('report_shader_population(true); // session end', capture)
        # dumped is process-wide, so each program is classified exactly once.
        self.assertIn('std::set<uint64_t> dumped;', capture)


if __name__ == '__main__':
    unittest.main()
