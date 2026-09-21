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

NAMES = ('fog_density_march', 'fog_density_composite', 'fog_density_repair', 'fog_density_march_exact')


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
        self.assertEqual([counts[n]['texture_instructions'] for n in NAMES], [17, 10, 22, 17])
        with self.assertRaises(ValueError):
            slots.count([0xffff0300])  # no END token

    def test_programs_are_registered_with_the_generator_and_use_inc_h(self):
        text = (ROOT / 'tools/shaders/generate_rigid_motion_pixel.py').read_text()
        for name in NAMES:
            self.assertIn("'%s': dict(" % name, text)
            self.assertTrue(slots.PROGRAMS[name].name.endswith('_program_inc.h'))
        self.assertEqual(slots.PROGRAMS['fog_density_march_exact'].parent, ROOT / 'verification/probe')

    def test_recorded_fixture_summary_passes_the_gates(self):
        s = json.loads((ROOT / 'verification/results/fog-density-shader/summary.json').read_text())
        self.assertEqual(s['result'], 'PASS'); self.assertTrue(all(s['gates'].values()))
        self.assertEqual(s['bottle']['name'], 'X3')
        t = s['versus_host']['bilinear32']['cand_T']
        self.assertLessEqual(t['p99'], .002); self.assertLessEqual(t['max'], .003)
        for name in NAMES:
            self.assertEqual(s['shaders'][name.replace('_', '-')]['bytecode_sha256'], record(name)['bytecode_sha256'])


if __name__ == '__main__':
    unittest.main()
