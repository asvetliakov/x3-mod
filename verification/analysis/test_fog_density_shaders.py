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
# The single look (FOG_LOOK), its quarter-resolution variant (step C) and the needs-repair census. (The 24-far-bin and
# visibility-grid variants were removed on 2026-09-25 with --fog-far-bins and --fog-shadow-pass.)
NAMES = BASE + tuple(slots.LOOK_PROGRAMS) + tuple(slots.Q4_PROGRAMS) + tuple(slots.CENSUS_PROGRAMS)
REMOVED = ('fog_density_march_look_far24', 'fog_density_repair_look_far24', 'fog_density_march_look_far24_q4', 'fog_density_repair_look_far24_q4',
           'fog_density_visibility_grid', 'fog_density_march_grid', 'fog_density_repair_grid', 'fog_dust_motes_grid')


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
        # The look: depth + 2x2 atlas fetches + 2 cascades x 4 shaft taps + the sun-ward tap's two far fetches (15);
        # composite 1+1+4+4; repair adds four footprint taps and the scene. The 64-bin march stays one loop.
        self.assertEqual([counts[n]['texture_instructions'] for n in slots.LOOK_PROGRAMS], [15, 10, 20])
        # Step C: the quarter-resolution programs are the scale-2 ones with other sample spacings: the same fetches, one loop
        # (composite none), slots within a few. The census quad: depth + four footprint depths, no loop.
        for name in slots.Q4_PROGRAMS:
            default = name.replace('_q4', '')
            self.assertEqual(counts[name]['texture_instructions'], counts[default]['texture_instructions'], name)
            self.assertEqual(counts[name]['loops'], counts[default]['loops'], name)
            self.assertLessEqual(abs(counts[name]['slots'] - counts[default]['slots']), 4, name)
        self.assertEqual([counts[n]['texture_instructions'] for n in slots.CENSUS_PROGRAMS], [5, 5])
        for name in NAMES:
            self.assertEqual(counts[name]['loops'], 0 if 'composite' in name or name in slots.CENSUS_PROGRAMS else 1, name)
        with self.assertRaises(ValueError):
            slots.count([0xffff0300])  # no END token

    def test_dust_mote_programs(self):
        # fog-dust-motes.md: the capsule pixel program over the shared field include (the grid variant went with the shadow
        # pass on 2026-09-25) and its vs_3_0 vertex program; recorded compilations, a fresh 512-slot budget.
        text = (ROOT / 'tools/shaders/generate_rigid_motion_pixel.py').read_text()
        for name, path in list(slots.MOTE_PROGRAMS.items()) + list(slots.MOTE_VERTEX.items()):
            r = record(name)
            self.assertIn("'%s': dict(" % name, text)
            self.assertEqual(r['source_sha256'], digest(ROOT / r['source']), name)
            self.assertEqual(r['header_sha256'], digest(path), name)
            for include, value in (r['includes'] or {}).items():
                self.assertEqual(value, digest(ROOT / include), (name, include))
            words = slots.words_of(path)
            self.assertEqual(r['bytecode_sha256'], hashlib.sha256(struct.pack('<%dI' % len(words), *words)).hexdigest(), name)
        self.assertEqual(record('fog_dust_motes_vertex')['target'], 'vs_3_0')
        counts = {name: slots.count(slots.words_of(path)) for name, path in slots.MOTE_PROGRAMS.items()}
        vertex = slots.count(slots.words_of(slots.MOTE_VERTEX['fog_dust_motes_vertex']), 'vs_3_0')
        for name, row in counts.items():
            self.assertLess(row['slots'], 512, name); self.assertEqual(row['loops'], 0, name)
            self.assertIn('src/fog/fog_density_field_inc.h', record(name)['includes'], name)
        # In-march: depth + 2x2 level fetches + 2 cascades x 4 shaft taps (13).
        self.assertEqual([counts[n]['texture_instructions'] for n in ('fog_dust_motes_look',)], [13])
        self.assertEqual(vertex['texture_instructions'], 0)  # no vertex texture fetch
        self.assertLess(vertex['slots'], 512)
        self.assertNotIn('FOG_SHADOW_PASS', (ROOT / record('fog_dust_motes_look')['source']).read_text())
        pass_source = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        for name in ('vertex', 'look'):
            self.assertIn('#include "fog_dust_motes_%s_program_inc.h"' % name, pass_source)
        self.assertNotIn('fog_dust_motes_grid', pass_source)

    def test_programs_are_registered_with_the_generator_and_use_inc_h(self):
        text = (ROOT / 'tools/shaders/generate_rigid_motion_pixel.py').read_text()
        for name in NAMES:
            self.assertIn("'%s': dict(" % name, text)
            self.assertTrue(slots.PROGRAMS[name].name.endswith('_program_inc.h'))
        self.assertEqual(slots.PROGRAMS['fog_density_march_exact'].parent, ROOT / 'verification/probe')
        # The removed variants are gone from the generator, the tree and the provenance records.
        for name in REMOVED:
            self.assertNotIn("'%s': dict(" % name, text)
            self.assertFalse((ROOT / ('src/renderer/%s_program_inc.h' % name)).exists(), name)
            self.assertFalse((ROOT / ('verification/results/%s-program.json' % name.replace('_', '-'))).exists(), name)

    def test_one_look_and_an_unshaped_parity_reference(self):
        # The look is the only law the renderer draws; the unshaped law survives behind #ifndef FOG_LOOK as the
        # fixture's parity reference (the presets L0/L1/L3 were retired on 2026-09-22, and no preset level remains).
        text = (ROOT / 'src/fog/fog_density_field_inc.h').read_text()
        guards = [line.split('//')[0].strip() for line in text.splitlines() if line.startswith(('#if', '#ifdef', '#ifndef')) and 'FOG_LOOK' in line]
        self.assertEqual(sorted(guards), sorted(['#ifdef FOG_LOOK'] * 3 + ['#if defined(FOG_LOOK)', '#ifndef FOG_LOOK', '#ifndef FOG_LOOK_NO_OFFSET']))
        self.assertNotIn('FOG_SHADOW_PASS', text); self.assertNotIn('FOG_GRID_PASS', text)
        self.assertNotIn('FOG_LOOK >=', text)
        for name in BASE:
            self.assertNotIn('FOG_LOOK', (ROOT / record(name)['source']).read_text(), name)
        for name in slots.LOOK_PROGRAMS:
            self.assertIn('#define FOG_LOOK\n', (ROOT / record(name)['source']).read_text(), name)
        # 40 far bins, the only count since the 24-bin variants went (default in the include, look law only).
        self.assertIn('#ifndef FOG_FAR_BINS\n#define FOG_FAR_BINS 40\n#endif', text)
        # Step C: the quarter-resolution variants define FOG_MARCH_SCALE 4 over the scale-2 sources; the default expands to the
        # literals it replaced (2.0 / 0.5), so the scale-2 programs keep their bytes (pinned below and by the fixture hashes).
        for name in list(slots.Q4_PROGRAMS) + ['fog_density_needs_census_q4']:
            source = (ROOT / record(name)['source']).read_text()
            self.assertIn('#define FOG_MARCH_SCALE 4\n', source, name)
            self.assertIn('#include "%s_ps.hlsl"' % name.replace('_q4', ''), source, name)
        self.assertIn('#ifndef FOG_MARCH_SCALE\n#define FOG_MARCH_SCALE 2\n#endif\n#if FOG_MARCH_SCALE == 2\n#define FOG_MARCH_STEP 2.0\n#define FOG_MARCH_INVERSE 0.5\n', text)
        self.assertNotIn('pixel*0.5', text); self.assertNotIn('q*2.0+0.5', text)
        self.assertIn('[loop] for (int i=0; i<24+FOG_FAR_BINS; ++i) {', text)
        self.assertIn('max(distance-12000.0,0.0)/float(FOG_FAR_BINS)', text)
        # The unshaped parity programs and the look programs are unchanged by the retirement: pinned bytecode.
        for name, digest in (('fog_density_march', '4dacf7e4d3ffa909cbd8b8a75352b44d55a471d36ba3222d977662cf6afda60f'),
                             ('fog_density_march_look', '6a347ac2c07d4be702c8b267f704b1aad2cdde16cd729565a2f01a0acf83ed25'),
                             ('fog_density_composite_look', '6c6a78b9fb72c4086e0a169ac249940d8eb4821a2eb457381e4333588e8ef369'),
                             ('fog_density_repair_look', '155a82e2833141db22e2f0688eb11c4aef2a614f612127a348fc739a49b85056')):
            self.assertEqual(record(name)['bytecode_sha256'], digest, name)
        # The renderer creates the three look programs and no unshaped one, and neither removed variant.
        pass_source = (ROOT / 'src/renderer/fog_pass.cpp').read_text()
        for name in ('march', 'composite', 'repair'):
            self.assertIn('#include "fog_density_%s_look_program_inc.h"' % name, pass_source)
            self.assertNotIn('#include "fog_density_%s_program_inc.h"' % name, pass_source)
        for name in REMOVED:
            self.assertNotIn('%s_program_inc.h' % name, pass_source)
        for name in slots.Q4_PROGRAMS:
            self.assertIn('#include "%s_program_inc.h"' % name, pass_source)
        for name in slots.CENSUS_PROGRAMS:
            self.assertIn('#include "%s_program_inc.h"' % name, pass_source)

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
        # The look: every case's GPU (S,T) against the host look_march inside the same gates, the fully shadowed sample
        # coloured, and the production pass drawing it. The default is the quarter-resolution march (fog-gpu-cost.md step C,
        # default since Run 77 C2): its look and repair split against the scale-4 reference, its images pinned.
        self.assertEqual(s['schema'], 3); self.assertEqual(s['default_march_scale'], 4)
        self.assertEqual((s['look_reference']['march_scale'], s['look_reference'].get('far_bins', 40)), (4, 40))
        self.assertNotIn('march_scale', s['reference'])  # the base reference: scale 2, 40 far bins
        for gate in ('look_cases', 'look_shaft_offset_exercised', 'repair_shaft_lookup', 'look_shadowed_coloured', 'pass_off_bit_identical', 'march_loops_kept', 'slots_below_512'):
            self.assertIs(s['gates'][gate], True, gate)
        self.assertNotIn('look0_shadowed_black', s['gates'])
        hashes = s['pass_off_hashes']
        self.assertEqual(hashes['measured'], hashes['expected']); self.assertEqual(len(hashes['expected']), 11)
        self.assertTrue(all('q4_' in k or k == 'repair_shafts_q4.full' for k in hashes['expected']))
        looks = s['look_versus_host']
        self.assertEqual(sorted(looks), ['A_look_depth3', 'A_look_depth90000', 'A_look_shadowed', 'A_look_sky', 'A_look_stripes', 'A_look_stripes_held', 'B_look_sky'])
        for label, row in looks.items():  # the quarter grid has 64x36 rays per case: at most 1 % sit at a noise wrap
            self.assertGreater(row['fogged'], 50, label)
            self.assertLessEqual(row['left_out_near_noise_wrap'], max(6, (row['compared'] + row['left_out_near_noise_wrap']) // 100), label)
            for variant in ('bilinear32', 'bilinear16'):
                self.assertLessEqual(row[variant]['T']['max'], .003, label); self.assertLessEqual(row[variant]['S']['max'], .003, label)
        self.assertGreater(looks['A_look_shadowed']['shadowed_min_S'], 0)
        self.assertLessEqual(s['repair_with_shafts']['versus_host']['max'], .003); self.assertGreater(s['repair_with_shafts']['other_law']['max'], .006)
        p = s['pass_fixture']
        self.assertGreaterEqual(p['checks'], 57); self.assertEqual(p['failed'], [])
        self.assertGreaterEqual(p['state_restorations'], 100)
        self.assertGreaterEqual(p['atlas_comparisons'], 10); self.assertEqual(p['atlas_differing_bytes'], 0)
        self.assertLessEqual(p['prepare_cpu']['max_upload_bytes'], 8 * 129 * 129 * 8)
        self.assertLessEqual(p['prepare_cpu']['max_update_surface_calls'], 64)
        self.assertEqual(p['fill']['nodes'], 2 * 128 ** 3)
        self.assertEqual(p['reset_reupload']['regenerated_nodes'], 0)
        self.assertGreater(p['repair']['half_pixel_shift_control'], 3 * p['repair']['worst_vs_cpu'])
        # Each set's record beside the summary, which stays under 50 KB and carries every gate: 15 spacing-independent,
        # 5 default look, 3 q4, 5 s2 (the scale-2 opt-out's look) = 28 (52 before the 7 far24_*, 7 s2_far24_* and 10 grid_*
        # gates went with their variants on 2026-09-25).
        self.assertLess((ROOT / 'verification/results/fog-density-shader/summary.json').stat().st_size, 50_000)
        self.assertEqual((s['march_scale_record_file'], s['scale2_variant_file']), ('q4.json', 's2.json'))
        self.assertNotIn('far_bins_variant_file', s); self.assertNotIn('visibility_grid', s)
        self.assertFalse(any(k.startswith(('far24_', 's2_far24_', 'grid_')) for k in s['gates']))
        self.assertEqual(len(s['gates']), 28)
        self.assertFalse((ROOT / 'verification/results/fog-density-shader/far24.json').exists())
        records = {}
        for name, prefix, count in (('q4.json', 'q4_', 3), ('s2.json', 's2_', 5)):
            r = records[name] = json.loads((ROOT / 'verification/results/fog-density-shader' / name).read_text())
            self.assertEqual(r['result'], 'PASS', name); self.assertEqual(r['bottle']['name'], 'X3', name)
            self.assertEqual(r['gates'], {k: v for k, v in s['gates'].items() if k.startswith(prefix)}, name); self.assertEqual(len(r['gates']), count, name)
        # Step C, the spacing: how far 4 moves the image from 2, the depth-edge chain, the quarter programs and pass checks.
        q = records['q4.json']
        self.assertEqual((q['reference']['march_scale'], q['reference'].get('far_bins', 40)), (4, 40))
        self.assertEqual(sorted(q['deviation_from_scale_2']), sorted(looks))
        e = q['depth_edges']
        self.assertGreater(e['needs_repair']['4'], e['needs_repair']['2']); self.assertLessEqual(e['repaired_gpu']['4'], e['needs_repair']['4'])
        self.assertGreaterEqual(len(q['pass_fixture_checks']), 10); self.assertTrue(all(v == 'PASS' for v in q['pass_fixture_checks'].values()))
        self.assertEqual(q['pass_fixture_checks']['march_scale_default_is_quarter'], 'PASS')
        for name in slots.Q4_PROGRAMS:
            self.assertEqual(s['shaders'][name.replace('_', '-')]['slots'], q['programs'][name.replace('_', '-')]['slots'])
        # The scale-2 opt-out: the former default look, byte for byte (its 11 accepted-look hashes).
        o = records['s2.json']
        self.assertEqual(o['march_scale'], 2); self.assertNotIn('march_scale', o['reference'])
        self.assertEqual(sorted(o['look_versus_host']), sorted(looks))
        self.assertEqual(o['pass_off_hashes']['measured'], o['pass_off_hashes']['expected']); self.assertEqual(len(o['pass_off_hashes']['expected']), 11)
        self.assertEqual(o['pass_off_hashes']['expected']['repair_shafts.full'], '88d6d32842e0a067')
        for label, row in o['look_versus_host'].items():
            for variant in ('bilinear32', 'bilinear16'):
                self.assertLessEqual(row[variant]['T']['max'], .003, label); self.assertLessEqual(row[variant]['S']['max'], .003, label)
        self.assertNotIn('far24', o)

if __name__ == '__main__':
    unittest.main()
