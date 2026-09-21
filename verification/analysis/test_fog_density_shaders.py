"""Stored-density fog shader fragments: provenance hashes, bytecode identity and slot limits."""
import hashlib
import json
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import fog_density_shader_slots as slots  # noqa: E402

BASE = ('fog_density_march', 'fog_density_composite', 'fog_density_repair', 'fog_density_march_exact')
NAMES = BASE + tuple(slots.LOOK_PROGRAMS)  # look presets L1-L3: FOG_LOOK variants of march, composite and repair


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record(name):
    return json.loads((ROOT / ('verification/results/%s-program.json' % name.replace('_', '-'))).read_text())


class FogDensityShaders(unittest.TestCase):
    def test_fragments_match_their_recorded_compilation(self):
        # A changed source, include or header without a regeneration fails here; the native
        # rebuild itself is `generate_rigid_motion_pixel.py --shader <name> --check` under the Wine lock.
        for name in NAMES:
            r = record(name); header = slots.PROGRAMS[name]
            self.assertEqual(r['source_sha256'], digest(ROOT / r['source']), name)
            self.assertEqual(r['header_sha256'], digest(header), name)
            self.assertIn('src/fog/fog_density_field_inc.h', r['includes'], name)
            for path, value in r['includes'].items():
                self.assertEqual(value, digest(ROOT / path), (name, path))
            words = slots.words_of(header)
            self.assertEqual(r['word_count'], len(words), name)
            self.assertEqual(r['bytecode_sha256'], hashlib.sha256(struct.pack('<%dI' % len(words), *words)).hexdigest(), name)

    def test_a_stale_header_is_detected(self):
        words = slots.words_of(slots.PROGRAMS['fog_density_march'])
        words[len(words) // 2] ^= 1
        self.assertNotEqual(record('fog_density_march')['bytecode_sha256'],
                            hashlib.sha256(struct.pack('<%dI' % len(words), *words)).hexdigest())

    def test_slot_and_fetch_counts(self):
        counts = {name: slots.count(slots.words_of(slots.PROGRAMS[name])) for name in NAMES}
        for name, row in counts.items():
            self.assertLess(row['slots'], 512, name)
        # Static texture instructions: depth + 2x2 atlas slices + 12 shaft taps; composite 1+1+4+4;
        # repair adds its four footprint taps and the scene; the exact variant has 16 atlas fetches.
        self.assertEqual([counts[n]['texture_instructions'] for n in BASE], [17, 10, 22, 17])
        # Look variants: depth + 2x2 atlas fetches + 2 cascades x 4 shaft taps (13); FOG_LOOK 2 adds the sun-ward tap's
        # two far fetches (15); repair adds four footprint taps and the scene. The 64-bin march stays one loop.
        self.assertEqual([counts[n]['texture_instructions'] for n in slots.LOOK_PROGRAMS], [13, 15, 10, 18, 20])
        for name in NAMES:
            self.assertEqual(counts[name]['loops'], 0 if 'composite' in name else 1, name)
        with self.assertRaises(ValueError):
            slots.count([0xffff0300])  # no END token

    def test_programs_are_registered_with_the_generator_and_use_inc_h(self):
        text = (ROOT / 'tools/shaders/generate_rigid_motion_pixel.py').read_text()
        for name in NAMES:
            self.assertIn("'%s': dict(" % name, text)
            self.assertTrue(slots.PROGRAMS[name].name.endswith('_program_inc.h'))
        self.assertEqual(slots.PROGRAMS['fog_density_march_exact'].parent, ROOT / 'verification/probe')

    def test_look_variants_leave_the_current_law_untouched(self):
        # L0 is the unshaped programs: every FOG_LOOK edit of the shared include sits behind the define.
        text = (ROOT / 'src/fog/fog_density_field_inc.h').read_text()
        self.assertEqual(text.count('#ifndef FOG_LOOK'), 1)
        for name in BASE:
            self.assertNotIn('FOG_LOOK', (ROOT / record(name)['source']).read_text(), name)
        for name in slots.LOOK_PROGRAMS:
            self.assertIn('#define FOG_LOOK ', (ROOT / record(name)['source']).read_text(), name)
        # Pinned by the commit that introduced the looks: the three production programs did not change.
        self.assertEqual(record('fog_density_march')['bytecode_sha256'], '4dacf7e4d3ffa909cbd8b8a75352b44d55a471d36ba3222d977662cf6afda60f')

    def test_recorded_fixture_summary_passes_the_gates(self):
        s = json.loads((ROOT / 'verification/results/fog-density-shader/summary.json').read_text())
        self.assertEqual(s['result'], 'PASS'); self.assertTrue(all(s['gates'].values()))
        self.assertEqual(s['bottle']['name'], 'X3')
        t = s['versus_host']['bilinear32']['cand_T']
        self.assertLessEqual(t['p99'], .002); self.assertLessEqual(t['max'], .003)
        for name in NAMES:
            self.assertEqual(s['shaders'][name.replace('_', '-')]['bytecode_sha256'], record(name)['bytecode_sha256'])
        # Checkpoint 3: display-scaled S gates, production RGBA16F and temporal rows inside the predicate,
        # no failing gate listed beside PASS, and the production FogPass fixture in the same run.
        self.assertEqual((s['gate_values']['S_p99'], s['gate_values']['S_max']), (.002, .003))
        self.assertNotIn('design_section5_S_gates', s)
        for gate in ('dense64_S', 'candidate_S', 'production_rgba16f_candidate', 'production_rgba16f_dense64', 'production_rgba16f_temporal', 'pass_fixture_passed'):
            self.assertIs(s['gates'][gate], True, gate)
        # Look presets: every preset's GPU (S,T) against the host look_march inside the same gates, the fully
        # shadowed sample black under L0 and coloured under L1, and the production pass cycling the presets.
        for gate in ('look_cases', 'look0_shadowed_black', 'look1_shadowed_coloured', 'march_loops_kept', 'slots_below_512'):
            self.assertIs(s['gates'][gate], True, gate)
        looks = s['look_presets_versus_host']
        self.assertEqual(sorted(looks), ['A_look1_depth90000', 'A_look1_shadowed', 'A_look1_sky', 'A_look2_depth3', 'A_look2_sky', 'A_look3_sky', 'B_look1_sky', 'B_look2_sky', 'B_look3_sky'])
        for label, row in looks.items():
            self.assertGreater(row['fogged'], 50, label); self.assertLessEqual(row['left_out_near_noise_wrap'], 6, label)
            for variant in ('bilinear32', 'bilinear16'):
                self.assertLessEqual(row[variant]['T']['max'], .003, label); self.assertLessEqual(row[variant]['S']['max'], .003, label)
        self.assertGreater(looks['A_look1_shadowed']['shadowed_min_S'], 0)
        p = s['pass_fixture']
        self.assertGreaterEqual(p['checks'], 58); self.assertEqual(p['failed'], [])
        self.assertGreaterEqual(p['state_restorations'], 100)
        self.assertGreaterEqual(p['atlas_comparisons'], 10); self.assertEqual(p['atlas_differing_bytes'], 0)
        self.assertLessEqual(p['prepare_cpu']['max_upload_bytes'], 8 * 129 * 129 * 8)
        self.assertLessEqual(p['prepare_cpu']['max_update_surface_calls'], 64)
        self.assertEqual(p['fill']['nodes'], 2 * 128 ** 3)
        self.assertEqual(p['reset_reupload']['regenerated_nodes'], 0)
        self.assertGreater(p['repair']['half_pixel_shift_control'], 3 * p['repair']['worst_vs_cpu'])


if __name__ == '__main__':
    unittest.main()
