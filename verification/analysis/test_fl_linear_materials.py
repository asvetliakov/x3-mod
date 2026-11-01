"""FL hull material rows of the original-shading variants (part 4b).

docs/architecture/farnhams-legacy-shaders.md: tools/analysis/inspect_fl_linear_materials.py
derives, offline from the FL archive programs, the Pixel/pair, XtPixel, diffuse-join
and HDR hull-gain rows of the 34 FL-only hull programs into five *_fl_inc.h includes
beside the unchanged AP tables. These tests check:

- the derived report (verification/results/fl-shader-coverage/linear-materials-fl.json)
  and the includes (no corpus needed);
- with the local FL corpus (X3M_FL_SHADER_PROGRAM_DIRECTORY, default
  /tmp/x3-fl-shader-sweep/programs and its manifest.json): the includes regenerate
  byte for byte, an AP collision is refused, and the AP structure oracles run over
  the FL rows: the light-map gain and widening oracles of test_hull_lightmap_gain /
  test_hull_emissive_widening (subclassed with the FL corpus and counts) and the fill,
  share, strength-twin, engine-light and HDR hull-gain drivers.
Without the corpus the corpus tests skip (X3M_REQUIRE_SHADER_CORPUS=1 fails instead).
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import inspect_fl_linear_materials as generator  # noqa: E402
from verification.analysis import test_hull_emissive_widening as widen_oracle  # noqa: E402
from verification.analysis import test_hull_lightmap_gain as gain_oracle  # noqa: E402

RESULTS = ROOT / 'verification/results/fl-shader-coverage'
REPORT = json.loads((RESULTS / 'linear-materials-fl.json').read_text())
PROGRAMS = {'ps_' + p['ps']: p for p in REPORT['programs']}
FAMILIES = {name: ('xt' if p['row']['family'] == 'xt' else 'hull') for name, p in PROGRAMS.items()}
FAMILIES.update({'vs_' + p['vs']: FAMILIES['ps_' + p['ps']] for p in REPORT['programs']})


def fl_programs():
    return Path(os.environ.get('X3M_FL_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-fl-shader-sweep/programs'))


def fl_corpus(owner):
    """A temporary directory holding only the 34 FL pixel programs and their 4 vertex programs."""
    source = fl_programs()
    if not all((source / f'{name}.bin').is_file() for name in FAMILIES):
        reason = f'local FL corpus unavailable under {source} (X3M_FL_SHADER_PROGRAM_DIRECTORY)'
        if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
            raise AssertionError(reason)
        raise unittest.SkipTest(reason)
    temporary = tempfile.TemporaryDirectory(prefix='x3-fl-linear-')
    owner.addClassCleanup(temporary.cleanup)
    corpus = Path(temporary.name) / 'corpus'
    corpus.mkdir()
    for name in FAMILIES:
        (corpus / f'{name}.bin').symlink_to(source / f'{name}.bin')
    return corpus


class ReportTests(unittest.TestCase):
    def test_counts_and_register_roles(self):
        self.assertEqual(REPORT['counts'], {'programs': 34, 'conventional': 22, 'xt': 12, 'pairs': 22,
                                            'exposure_seeds': 34, 'hull_emitters': 12, 'join_product': 30})
        self.assertEqual(REPORT['hull_emitter_rule']['reproduces_ap_rows'], 12)
        for p in REPORT['programs']:
            row = p['row']
            self.assertEqual((p['join']['lanes'], p['join']['depth'], p['lightmap_flow_depth']), (7, 0, 0), p['ps'])
            if row['family'] == 'conventional':
                # FL layout (fl/materials.md): colours c6/c8, glow c4, affine rows c1-c3.
                self.assertEqual((row['light0'], row['light1'], row['glow'], row['affine_first']), (6, 8, 4, 1), p['ps'])
                self.assertEqual(row['temporary_base'], 7 if row['bump'] else
                                 6 if p['ps'] in ('0d5848c8489b6630', '33826e49b53b88dc', '5fb88b63dd609443',
                                                  'ced7edef48059e84') else 5, p['ps'])
            else:
                self.assertEqual((row['light0'], row['light1'], row['palette_first']),
                                 (7, 9, 19 if row['bump'] else 16), p['ps'])
                self.assertEqual(row['temporary_base'], 6 if not row['terra'] or p['ps'] == '87ccd863f19254b2' else 7)

    def test_includes_are_fl_only_and_wired(self):
        self.assertTrue(all(name.endswith('_fl_inc.h') for name in generator.OUTPUTS.values()))
        material = (ROOT / 'src/renderer/linear_material.cpp').read_text()
        xt = (ROOT / 'src/renderer/linear_xt_material_inc.h').read_text()
        emission = (ROOT / 'src/renderer/linear_emission.cpp').read_text()
        for name, text in (('linear_material_pixels_fl_inc.h', material), ('linear_material_pairs_fl_inc.h', material),
                           ('material_exposure_profiles_fl_inc.h', material), ('linear_xt_profiles_fl_inc.h', xt),
                           ('linear_emission_hull_fl_inc.h', emission)):
            self.assertIn(f'#include "{name}"', text)
            rows = (ROOT / 'src/renderer' / name).read_text().count('ull,')
            self.assertGreater(rows, 0, name)
        # FL rows admit the original-shading variants only.
        self.assertIn('if (p && p->original_only) return LinearMaterialResult::UnsupportedShader;', material)
        self.assertIn('if (xt_fl_row(p)) return LinearMaterialResult::UnsupportedShader;', xt)
        self.assertIn('static_assert(reviewed_pixel_max_words() == original_pixel_max_words', material)


class GeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fl_corpus(cls)
        cls.manifest = fl_programs().parent / 'manifest.json'
        if not cls.manifest.is_file():
            raise unittest.SkipTest('FL sweep manifest unavailable')

    def arguments(self, output):
        return mock.Mock(programs=fl_programs(), manifest=self.manifest, motion=RESULTS / 'motion-output-profiles-fl.json',
                         motion_header=ROOT / 'src/renderer/motion_output_profiles_fl_inc.h',
                         base_motion=ROOT / 'verification/results/motion-output-profiles.json', output=output)

    def test_includes_and_report_regenerate_byte_for_byte(self):
        run = subprocess.run([sys.executable, str(ROOT / 'tools/analysis/inspect_fl_linear_materials.py'), '--check',
                              '--programs', str(fl_programs()), '--manifest', str(self.manifest)],
                             capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        report = generator.derive(self.arguments(None))
        report_text = json.dumps(report, indent=1) + '\n'
        self.assertEqual(report_text, (RESULTS / 'linear-materials-fl.json').read_text())

    def test_refuses_a_row_an_ap_table_already_has(self):
        collision = generator.ap_hashes() | {REPORT['programs'][0]['ps']}
        with mock.patch.object(generator, 'ap_hashes', return_value=collision):
            with self.assertRaisesRegex(ValueError, 'collides with an AP row'):
                generator.derive(self.arguments(None))


class FlHullLightmapGainOracle(gain_oracle.HullLightmapGainTransformerTests):
    GAINED = 34
    UNTOUCHED = set()  # every FL hull program fetches a light map
    EXPECTED_REGISTERS = {}
    EXPECTED_STAGES = {}
    for _p in REPORT['programs']:
        _r = _p['row']
        _register = 0 if _r['family'] == 'conventional' else _r['texture_registers'][_r['lightmap_stage']]
        EXPECTED_REGISTERS[_register] = EXPECTED_REGISTERS.get(_register, 0) + 1
        EXPECTED_STAGES[_r['lightmap_stage']] = EXPECTED_STAGES.get(_r['lightmap_stage'], 0) + 1

    @classmethod
    def corpus(cls):
        return fl_corpus(cls), dict(FAMILIES)

    def test_coverage_100_programs_gain_and_8_without_the_term_stay(self):
        by_family = {}
        for name in self.covered():
            row = self.rows[name]
            self.assertEqual((row['status'], row['gain_applied'], row['widen_applied'], row['share_gain_applied']),
                             (0, 1, 1, 1), name)
            by_family[self.families[name]] = by_family.get(self.families[name], 0) + 1
        self.assertEqual(by_family, {'hull': 22, 'xt': 12})
        self.assertEqual((self.driver['supported'], self.driver['applied'], self.driver['untouched']), (34, 34, 0))


class FlHullEmissiveWideningOracle(widen_oracle.HullEmissiveWideningTransformerTests):
    GAINED = 34
    UNTOUCHED = set()
    EXPECTED_FAMILIES = {'hull': 22, 'xt': 12}
    test_coverage_every_sm3_lightmap_program_is_widened_or_listed = None  # the AP archive census

    @classmethod
    def corpus(cls):
        return fl_corpus(cls), dict(FAMILIES)


class FlStructureDrivers(unittest.TestCase):
    """The fill, share, strength-twin, engine-light and HDR hull-gain drivers over the FL rows: each
    driver's own structural requires (byte relations to its control, slots, refusals) must hold."""

    @classmethod
    def setUpClass(cls):
        cls.corpus = fl_corpus(cls)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        cls.directory = cls.corpus.parent
        cls.results = {}
        material = [str(ROOT / 'src/renderer/linear_material.cpp'), str(ROOT / 'src/renderer/material_motion.cpp')]
        hull = sorted(p['ps'] for p in REPORT['programs'] if p['hull_emitter'])
        for driver, sources, extra in (
                ('original_fill_structure', material, []),
                ('original_sun_share_structure', material[1:], []),
                ('material_strength_twin_structure', material, []),
                ('engine_light_structure', material, []),
                ('linear_emission_hull_gain_structure', [], hull)):
            executable, out = cls.directory / driver, cls.directory / ('out-' + driver)
            out.mkdir()
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / f'verification/probe/{driver}.cpp'), *sources, '-o', str(executable)],
                                   capture_output=True, text=True)
            if build.returncode:
                raise AssertionError(build.stdout + build.stderr)
            corpus = fl_programs() if extra else cls.corpus
            run = subprocess.run([str(executable), str(corpus), str(out), *extra], capture_output=True, text=True)
            if run.returncode:
                raise AssertionError(driver + ': ' + run.stdout[-2000:] + run.stderr)
            cls.results[driver] = json.loads(run.stdout)

    def rows(self, driver):
        return [row for row in self.results[driver]['rows'] if row.get('name', 'ps_').startswith('ps_')]

    def test_fill_applies_to_all_34(self):
        result = self.results['original_fill_structure']
        self.assertEqual((result['supported'], result['applied']), (34, 34))
        self.assertTrue(all(row['status'] == 0 and row['fill_applied'] for row in self.rows('original_fill_structure')))

    def test_share_producer_applies_to_all_34_with_the_row_light(self):
        result = self.results['original_sun_share_structure']
        self.assertEqual((result['supported'], result['applied']), (34, 34))
        for row in self.rows('original_sun_share_structure'):
            self.assertEqual((row['status'], row['share_applied']), (0, 1), row['name'])
            self.assertEqual(row['light'], PROGRAMS[row['name']]['row']['light0'], row['name'])

    def test_every_strength_twin_applies(self):
        rows = self.results['material_strength_twin_structure']['rows']
        self.assertEqual(self.results['material_strength_twin_structure']['programs'], 34)
        self.assertEqual(len(rows), 34 * 15)
        self.assertEqual({(row['status'], row['refusal']) for row in rows}, {(0, 'none')})

    def test_every_engine_light_twin_applies(self):
        result = self.results['engine_light_structure']
        self.assertEqual((result['programs'], result['twins']), (34, 34 * 8))

    def test_hdr_hull_gain_covers_the_twelve_fl_programs(self):
        result = self.results['linear_emission_hull_gain_structure']
        self.assertEqual((result['programs'], result['modulated'], result['variants'], result['identical']),
                         (12, 6, 48, 12))


if __name__ == '__main__':
    unittest.main()
