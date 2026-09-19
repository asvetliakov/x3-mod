"""Host oracle of the hull self-illumination gain (--hull-lightmap-gain).

Transformer (linear_material_hull_lightmap_gain_pixel_variant): G=1 is byte
for byte the original-fill variant of the original pixel program (K=0: the
plain motion/depth variant); G>1 adds, in the 100 reviewed programs that add a
light-map term, exactly one `def c223 = (G, 0, 0, 0)` before the first
declaration and one `mul rL.xyz, rL, c223.x` immediately after the light-map
fetch (`texld rL, vN, s{2|3}`, the last texture read of the program), every
other word retained in order; the four glass and four asteroid programs have
no such term and keep the fill variant byte for byte
(docs/reverse-engineering/hull-self-illumination.md 5). Launcher gate:
requires --hdr, excludes --linear-materials, composes with --original-fill.
Ctrl+Shift+F4 toggles the gain together with the hull emitters. No game
assets bundled.
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import inspect_motion_output_profiles as shader
import run_linear_material as runner
from verification.analysis.test_capture_bloom_lifetime import extract_function
from verification.analysis.test_original_fill import families, launch

GAIN_CONSTANT = 223
GAIN = 4.0
FILLS = (0., 0.05)
DEF, MUL, TEXLD, SAMPLER = 81, 5, 66, 10
PREREQUISITES = ['--motion-output', '--hdr']
UNTOUCHED = {'ps_517540ae6d5e5410', 'ps_550c2a4d4d3ed70f', 'ps_7a0c3388065bb08d', 'ps_d44db87778a43b61',
             'ps_9d49f288800f898d', 'ps_a66fb1981ba755b2', 'ps_ebc9b2b3f1564e9a', 'ps_f31c9e2701c8eee4'}


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


class HullLightmapGainTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        cls.families = families()
        if not all((cls.originals / f'{name}.bin').is_file() for name in cls.families):
            reason = f'local original corpus of the 137 reviewed programs unavailable under {cls.originals} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            print('SKIP:', reason, file=sys.stderr)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-hull-lightmap-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/hull_lightmap_gain_structure.cpp'),
                                str(ROOT / 'src/renderer/linear_material.cpp'), str(ROOT / 'src/renderer/material_motion.cpp'),
                                '-o', str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable), str(cls.originals), str(cls.directory)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)
        cls.rows = {row['name']: row for row in cls.driver['rows']}

    def covered(self):
        return sorted(name for name, family in self.families.items() if name.startswith('ps_'))

    def gained(self):
        return [name for name in self.covered() if name not in UNTOUCHED]

    def test_coverage_100_programs_gain_and_8_without_the_term_stay(self):
        by_family = {}
        for name in self.covered():
            row = self.rows[name]
            self.assertEqual(row['status'], 0, name)
            by_family.setdefault(self.families[name], [0, 0])
            by_family[self.families[name]][0] += 1
            by_family[self.families[name]][1] += row['gain_applied']
        self.assertEqual({k: tuple(v) for k, v in by_family.items()},
                         {'hull': (66, 66), 'asteroid': (4, 0), 'palette': (20, 20), 'xt': (14, 14), 'glass': (4, 0)})
        self.assertEqual((self.driver['supported'], self.driver['applied'], self.driver['untouched']), (108, 100, 8))
        self.assertEqual({name for name in self.covered() if not self.rows[name]['gain_applied']}, UNTOUCHED)
        for name in self.families:
            if name.startswith('vs_'):
                self.assertEqual(self.rows[name]['status'], 3, (name, 'vertex programs: UnsupportedShader'))
        admitted = {name for name, row in self.rows.items() if row['status'] == 0}
        self.assertEqual(admitted, set(self.covered()))

    def test_far_fade_dynamic_variant_drops_the_def_and_reads_c217_w(self):
        # --light-map-far-fade: the driver rebuilds every dynamic variant from
        # the constant-gain one (DEF removed, c223.x -> c217.w) and compares it
        # byte for byte: 100 programs x (2 fills x 2 depth modes + 2 share).
        self.assertEqual(self.driver['dynamic_checks'], 100 * 6)

    def test_untouched_programs_keep_the_fill_variant_byte_for_byte(self):
        for name in sorted(UNTOUCHED):
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    base = (self.directory / f'{name}-hlbase-{f}-{depth}.bin').read_bytes()
                    self.assertEqual((self.directory / f'{name}-hlgain-{f}-{depth}.bin').read_bytes(), base, (name, f, depth))

    def test_share_producer_carries_the_same_fragment(self):
        """The lane's original share variant (share producer + fill K) with the gain:
        the same verdict per program, the same DEF and MUL and nothing else added,
        the share reduction (c212/c221, r11-r23) retained; the untouched eight
        keep the share variant byte for byte."""
        for name in self.covered():
            row = self.rows[name]
            self.assertEqual(row['share_gain_applied'], row['gain_applied'], name)
            for f in range(len(FILLS)):
                share = (self.directory / f'{name}-hlshare-{f}.bin').read_bytes()
                gained = (self.directory / f'{name}-hlsharegain-{f}.bin').read_bytes()
                if name in UNTOUCHED:
                    self.assertEqual(gained, share, (name, f))
                    continue
                self.assertEqual(row['share_variant_slots'][f] - row['share_slots'][f], 1, name)
                words, items, added = self.added_items(name, f, None, share=True)
                self.assertEqual([item['opcode'] for item in added], [DEF, MUL], (name, f))
                self.assertEqual(shader.register_of(added[0]['words'][0]), (2, GAIN_CONSTANT))
                self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *added[0]['words'][1:])), (f32(GAIN), 0., 0., 0.), name)
                fetch = items[items.index(added[1]) - 1]
                self.assertEqual(fetch['opcode'], TEXLD, name)
                self.assertEqual(shader.register_of(fetch['words'][0]), shader.register_of(added[1]['words'][0]), name)
                # The plain variant's fragment is the same two instructions.
                _, _, plain = self.added_items(name, f, 1)
                self.assertEqual([tuple(i['words']) for i in plain], [tuple(i['words']) for i in added], (name, f))
                # The share reduction is still there: c221 luma weights read, oC2.g written.
                self.assertTrue(any((2, 221) in [shader.register_of(w) for w in item['words'][1:]] for item in items), (name, 'share reduction'))
                self.assertTrue(any(shader.register_of(item['words'][0]) == (8, 2) for item in items if item['words'] and item['opcode'] not in (DEF, shader.DCL)), (name, 'oC2'))

    def added_items(self, name, f, depth, share=False):
        prefix = (f'{name}-hlshare-{f}', f'{name}-hlsharegain-{f}') if share else (f'{name}-hlbase-{f}-{depth}', f'{name}-hlgain-{f}-{depth}')
        base, base_items, _ = shader.instructions((self.directory / f'{prefix[0]}.bin').read_bytes())
        words, items, _ = shader.instructions((self.directory / f'{prefix[1]}.bin').read_bytes())
        base_spans = [span(base, b) for b in base_items]
        added, retained = [], []
        for item in items:
            row = span(words, item)
            if row in base_spans and base_spans.count(row) > sum(1 for r in retained if r == row):
                retained.append(row)
            else:
                added.append(item)
        self.assertEqual(retained, base_spans, (name, f, depth, 'fill/motion program retained in order'))
        return words, items, added

    def test_gain_adds_the_exact_def_and_mul_after_the_last_texture_read(self):
        registers, stages = {}, {}
        for name in self.gained():
            family = self.families[name]
            for f, fill in enumerate(FILLS):
                for depth in (0, 1):
                    words, items, added = self.added_items(name, f, depth)
                    self.assertEqual(len(added), 2, (name, f, depth))
                    definition, gain = added
                    self.assertEqual(definition['opcode'], DEF, name)
                    self.assertEqual(shader.register_of(definition['words'][0]), (2, GAIN_CONSTANT))
                    self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *definition['words'][1:])), (f32(GAIN), 0., 0., 0.), name)
                    first_dcl = next(item for item in items if item['opcode'] == shader.DCL)
                    self.assertLess(definition['dword'], first_dcl['dword'], name)
                    # mul rL.xyz, rL, c223.x: no saturate, no partial precision, no modifiers.
                    self.assertEqual(gain['opcode'], MUL, name)
                    dst, src, const = gain['words']
                    self.assertEqual(dst & 0x00f00000, 0, (name, 'no dst modifier'))
                    self.assertEqual(shader.register_of(dst)[0], 0)
                    rl = shader.register_of(dst)[1]
                    self.assertEqual(shader.mask_of(dst), 'xyz')
                    self.assertEqual((shader.register_of(src), shader.swizzle_of(src), src >> 24 & 0xf), ((0, rl), 'xyzw', 0), name)
                    self.assertEqual((shader.register_of(const), shader.swizzle_of(const)), ((2, GAIN_CONSTANT), 'xxxx'), name)
                    # Immediately after the light-map fetch: texld rL, vN, s{2|3}, the last texld of the program.
                    fetch = items[items.index(gain) - 1]
                    self.assertEqual(gain['dword'], fetch['dword'] + fetch['length'] + 1, name)
                    self.assertEqual(fetch['opcode'], TEXLD, name)
                    self.assertEqual((shader.register_of(fetch['words'][0]), shader.mask_of(fetch['words'][0])), ((0, rl), 'xyzw'), name)
                    self.assertEqual(shader.register_of(fetch['words'][1])[0], 1, (name, 'a varying coordinate'))
                    kind, stage = shader.register_of(fetch['words'][2])
                    self.assertEqual(kind, SAMPLER, name)
                    self.assertIn(stage, (2, 3), name)
                    self.assertEqual(fetch, [item for item in items if item['opcode'] == TEXLD][-1], (name, 'last texture read'))
                    # Later: rL.xyz is written by nothing but the terra MAD (which reads it) and read by the
                    # final colour instruction; no other instruction reads c223, no original defines it.
                    later = items[items.index(gain) + 1:]
                    writers = [item for item in later if item['words'] and shader.register_of(item['words'][0]) == (0, rl)
                               and set(shader.mask_of(item['words'][0])) & set('xyz') and item['opcode'] not in (shader.DCL, DEF)]
                    self.assertLessEqual(len(writers), 1, name)
                    if writers:
                        self.assertEqual(writers[0]['opcode'], 4, (name, 'terra MAD'))
                        self.assertEqual((shader.register_of(writers[0]['words'][3]), shader.swizzle_of(writers[0]['words'][3])), ((0, rl), 'xyzw'), name)
                        self.assertEqual(family, 'xt', name)
                    readers = [item for item in later if any(shader.register_of(w) == (0, rl) and set(shader.swizzle_of(w)) & set('xyz')
                                                             for w in item['words'][1:]) and item['opcode'] not in (shader.DCL, DEF)]
                    final = [item for item in readers if shader.register_of(item['words'][0])[0] in (8, 0) and item not in writers]
                    self.assertEqual(len(final), 1, (name, 'one consumer of the gained light map'))
                    self.assertIn(final[0]['opcode'], (2, 4), (name, 'add or mad'))
                    self.assertEqual(shader.register_of(final[0]['words'][0]), (8, 0), (name, 'oC0'))
                    self.assertEqual(shader.mask_of(final[0]['words'][0]), 'xyz', name)
                    for item in items:
                        if item is gain or item is definition:
                            continue
                        self.assertNotIn((2, GAIN_CONSTANT), [shader.register_of(w) for w in item['words']], (name, 'c223 collision'))
                    if f == 0 and depth == 0:
                        registers[rl] = registers.get(rl, 0) + 1
                        stages[stage] = stages.get(stage, 0) + 1
        self.assertEqual(registers, {0: 94, 1: 6})
        self.assertEqual(stages, {2: 44, 3: 56})

    def test_slot_budget_and_constant_reservation(self):
        for name in self.gained():
            row = self.rows[name]
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    self.assertEqual(row['variant_slots'][f][depth] - row['base_slots'][f][depth], 1, name)
                    self.assertEqual(row['variant_instructions'][f][depth] - row['base_instructions'][f][depth], 1, name)
                    self.assertLessEqual(row['variant_slots'][f][depth], 512, name)
        self.assertEqual(self.driver['max_variant_slots'], max(max(max(max(v) for v in self.rows[n]['variant_slots']), max(self.rows[n]['share_variant_slots'])) for n in self.gained()))
        self.assertLess(self.driver['max_variant_slots'], 512)
        # c223 sits above every game-declared PS constant (c23; the originals read at most c26),
        # outside the linear-material DEFs (c212-c213), the fade producer (c214), the fill (c215),
        # the motion ABI (c216-c220) and the share/exposure literals (c221-c222); the same last
        # ps_3_0 constant the ONE/ONE hull-emission variant defines locally.
        self.assertGreater(GAIN_CONSTANT, 26)
        self.assertNotIn(GAIN_CONSTANT, shader.PIXEL_ABI_CONSTANTS)
        self.assertNotIn(GAIN_CONSTANT, (212, 213, 214, 215, 221, 222))
        source = (ROOT / 'src/renderer/linear_material.cpp').read_text()
        self.assertIn('constexpr unsigned lightmap_gain_constant = 223;', source)
        self.assertIn('emit(out,mul,{dst(temp,reg),src(temp,reg),lightmap_gain_operand(dynamic)});', source)
        self.assertIn('return dynamic ? lane(constant,lightmap_dynamic_constant,3) : lane(constant,lightmap_gain_constant,0);', source)
        self.assertIn('constexpr unsigned hull_gain_constant = 223', (ROOT / 'src/renderer/linear_emission.cpp').read_text())


class LauncherAndProxyGateTests(unittest.TestCase):
    def test_light_map_far_fade_launcher_and_law(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
            baseline = json.loads(output)['env']
            self.assertEqual(baseline['X3M_LIGHT_MAP_FAR_FADE'], '80,220,1')
            with mock.patch.dict(os.environ, {'X3M_LIGHT_MAP_FAR_FADE': '1,2'}):
                code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env'], baseline)  # an inherited value is dropped
            for args in (PREREQUISITES + ['--no-light-map-far-fade'], PREREQUISITES + ['--hull-lightmap-gain', '1'], PREREQUISITES + ['--linear-materials', '--hdr-tonemap'], ['--motion-output']):
                with mock.patch.dict(os.environ, {'X3M_LIGHT_MAP_FAR_FADE': '1,2'}):
                    code, output, error = launch(directory, *args)
                self.assertEqual(code, 0, error)
                self.assertNotIn('X3M_LIGHT_MAP_FAR_FADE', json.loads(output)['env'])
            code, _, error = launch(directory, *PREREQUISITES, '--no-light-map-far-fade', '--light-map-far-fade', '80,220')
            self.assertEqual(code, 2)
            for value, expected in (('60,120', '60,120,1'), ('60,120,0', '60,120,0'), ('60,120,4', '60,120,4'), ('0.5,1e6,2.5', '0.5,1e+06,2.5')):
                code, output, error = launch(directory, *PREREQUISITES, '--light-map-far-fade', value); self.assertEqual(code, 0, error)
                env = json.loads(output)['env']
                self.assertEqual(env.pop('X3M_LIGHT_MAP_FAR_FADE'), expected)
                self.assertEqual(env, {k: v for k, v in baseline.items() if k != 'X3M_LIGHT_MAP_FAR_FADE'})
            for value in ('', '60', '60,120,1,2', '120,60', '0,60', '60,60', '60,120,4.5', '60,120,-1', 'a,b', '60,nan', '60,2e6'):
                code, _, error = launch(directory, *PREREQUISITES, '--light-map-far-fade', value)
                self.assertEqual(code, 2, value); self.assertIn('--light-map-far-fade', error)
            for extra in (['--hull-lightmap-gain', '1'], ['--linear-materials']):
                code, _, error = launch(directory, *PREREQUISITES, *extra, '--light-map-far-fade', '60,120')
                self.assertEqual(code, 2, extra)
            code, _, error = launch(directory, '--motion-output', '--light-map-far-fade', '60,120'); self.assertEqual(code, 2)
            code, _, error = launch(directory, *PREREQUISITES, '--hull-lightmap-gain', '2', '--light-map-far-fade', '60,120,3'); self.assertEqual(code, 2)
        core = (ROOT / 'src/proxy/fade_route_core.h').read_text()
        law = extract_function(core, 'inline float lightmap_far_gain(')
        for text in ('if (!camera_valid || !(m00 > 0.f) || !(width > 0.f) || !(w > 0.f)) return gain;', 'if (!(t > 0.f)) return gain;',
                     'if (t >= 1.f) return floor;', 'return gain + (floor - gain) * t;'):
            self.assertIn(text, law)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        draw = extract_function(motion, 'void MotionOutput::evaluate_draw(')
        # The gain rides the motion ABI's own two-vector upload: no additional constant write, no Get*.
        self.assertIn('matched ? 1.f : 0.f, 0.f, 0.f, lightmap_fade_gain_};', draw)
        self.assertEqual(draw.count('SetPixelShaderConstantF'), 1)
        self.assertIn('lightmap_far_fade_ && (shadow_.hull_lightmap_pair || shadow_.ps_sun_original_lightmap)', draw)
        # Every program that reads c217.w gets it: the two gained selections of the one bind site are covered by the
        # upload's gate (plain: hull_lightmap_pair; share, the lane's cutout pairs included: ps_sun_original_lightmap),
        # and the fade-band / overlay arm never binds a gained variant.
        bind = extract_function(motion, 'HRESULT MotionOutput::bind_variant_pair(')
        self.assertEqual(bind.count('ps = shadow_.ps_hull_lightmap_variant;'), 1)
        self.assertIn('if (shadow_.hull_lightmap_pair && hull_lightmap_enabled_', bind)
        self.assertIn('&& !route.fade_arm && shadow_.ps_hull_lightmap_variant) {', bind)
        self.assertIn('} else if(sun_lane_active_&&route.depth&&!route.fade_arm){', bind)
        self.assertEqual(bind.count('gained_original?shadow_.ps_sun_original_lightmap:'), 1)
        self.assertEqual(motion.count('bind_variant_pair(route, material)'), 1)
        # The latch is armed only for a value the DLL parser accepted, and never feeds camera_scene_ on its own.
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('camera_state::request_consumer();} // the footprint', capture)
        self.assertIn('parsed[1]<=1e6f', capture)
        self.assertNotIn('X3M_LIGHT_MAP_FAR_FADE', (ROOT / 'src/proxy/camera_state.cpp').read_text())
        read = extract_function(motion, 'void MotionOutput::read_camera(bool scene) noexcept')
        self.assertLess(read.index('if (!(taa_enabled_ || candidates_requested_)) {'), read.index('camera_scene_ = sample.state;'))
        configure = extract_function(motion, 'bool MotionOutput::configure_lightmap_far_fade(')
        self.assertIn('if (device_) return lightmap_far_fade_;', configure)

    def test_default_off_requires_hdr_excludes_linear_materials_and_composes_with_the_fill(self):
        with tempfile.TemporaryDirectory() as directory:
            # Launcher default (user selection after run 41 C): 4 in HDR mode,
            # the same environment an explicit --hull-lightmap-gain 4 writes;
            # the DLL's own default stays 1 = off.
            code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
            baseline = json.loads(output)['env']
            self.assertEqual(baseline['X3M_HULL_LIGHTMAP_GAIN'], '4.0')
            code, output, error = launch(directory, *PREREQUISITES, '--hull-lightmap-gain', '4'); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_HULL_LIGHTMAP_GAIN'], '4.0')
            self.assertEqual(env, baseline)
            # Without --hdr the option is refused, but the default never fires:
            # the variable is written off, with no error.
            code, output, error = launch(directory, '--motion-output'); self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_HULL_LIGHTMAP_GAIN'], '1.0')
            # An explicit 1 turns the default off again.
            code, output, error = launch(directory, *PREREQUISITES, '--hull-lightmap-gain', '1'); self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_HULL_LIGHTMAP_GAIN'], '1.0')
            linear = ['--hdr-tonemap', '--linear-materials']
            for bad in (('--motion-output', '--hull-lightmap-gain', '4'),
                        (*PREREQUISITES, *linear, '--hull-lightmap-gain', '4'),
                        (*PREREQUISITES, *linear, '--hull-lightmap-gain', '1'),
                        (*PREREQUISITES, '--hull-lightmap-gain', '0.5'), (*PREREQUISITES, '--hull-lightmap-gain', '8.5'),
                        (*PREREQUISITES, '--hull-lightmap-gain', 'nan'), (*PREREQUISITES, '--hull-lightmap-gain', 'inf')):
                code, _, error = launch(directory, *bad); self.assertEqual(code, 2, bad); self.assertIn('--hull-lightmap-gain', error)
            for boundary in ('1', '8'):
                code, output, error = launch(directory, *PREREQUISITES, '--hull-lightmap-gain', boundary); self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_HULL_LIGHTMAP_GAIN'], repr(float(boundary)))
            code, output, error = launch(directory, *PREREQUISITES, '--hull-lightmap-gain', '4', '--original-fill', '0.05'); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_HULL_LIGHTMAP_GAIN'], env['X3M_ORIGINAL_FILL']), ('4.0', '0.05'))
            # With linear materials the converted route's --lightmap-emissive-gain applies instead.
            code, output, error = launch(directory, *PREREQUISITES, *linear, '--lightmap-emissive-gain', '4'); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_HULL_LIGHTMAP_GAIN'], env['X3M_LIGHTMAP_EMISSIVE_GAIN']), ('1.0', '4.0'))

    def test_dll_gate_creation_selection_and_the_shared_f4_toggle(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = source[source.index('X3M_HULL_LIGHTMAP_GAIN=<g>'):][:2200]
        self.assertIn('GetEnvironmentVariableW(L"X3M_HULL_LIGHTMAP_GAIN",setting,32)', block)
        self.assertIn('value>=1.f&&value<=8.f', block)
        self.assertIn('const bool excluded=linear_material_requested;', block)
        self.assertIn('if(!hdr_requested||excluded)hull_lightmap_gain=1.f;', block)
        self.assertIn('configure_hull_lightmap_gain(hull_lightmap_gain)', source)
        for absent in ('taa_requested', 'screen_ownership'):
            self.assertNotIn(absent, block)
        polling = extract_function(source, 'void comparison_begin_frame(')
        self.assertIn('|| hull_emission_gain!=1.f || hull_lightmap_gain!=1.f;', polling)
        self.assertIn('if(action.hull_gain)comparison_emitter(ctx,"ctrl_shift_f4","LIGHTMAP",ctx.motion_output.hull_emission_gain_toggle(true));', polling)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('hull_lightmap_gain_requested_ = std::isfinite(gain) && gain > 1.f && gain <= 8.f && !linear_material_requested_;', motion)
        # Created once at registration beside the fill variant, composed with the fill K.
        self.assertIn('renderer::linear_material_hull_lightmap_gain_pixel_variant(', motion)
        self.assertIn('original_fill_requested_ ? original_fill_ : 0.f, hull_lightmap_gain_,', motion)
        self.assertIn('hull_lightmap_variant device=%llu original=%016llx transform=%u create=%08lx words=%u depth=%u fill=%g gain=%g fill_applied=%u gain_applied=%u', motion)
        # Selected in the one bind pair over the plain or fill variant while the F4 flag is on; undone with the route.
        bind = extract_function(motion, 'HRESULT MotionOutput::bind_variant_pair(')
        self.assertIn('shadow_.hull_lightmap_pair && hull_lightmap_enabled_ && !material && !shadow_.xt_default_ready && hdr_state_ == HdrState::Active', bind)
        self.assertIn('(ps == shadow_.ps_variant || (fill && ps == shadow_.ps_original_fill_variant))', bind)
        self.assertIn('route.hull_lightmap = true', bind)
        self.assertLess(bind.index('ps = shadow_.ps_original_fill_variant; fill = true;'), bind.index('ps = shadow_.ps_hull_lightmap_variant; lightmap = true;'))
        self.assertNotIn('GetRenderState', bind)
        contract = motion[motion.index('void MotionOutput::refresh_linear_material_contract'):][:3000]
        self.assertIn('shadow_.hull_lightmap_pair = hull_lightmap_gain_requested_ && shadow_.ps_hull_lightmap_variant && shadow_.vs_variant', contract)
        # F4 is the light-map gain alone (its own flag); the guide lights moved
        # to F6 with the effects gain. Both states and the driving key are logged.
        toggle = extract_function(motion, 'int MotionOutput::hull_emission_gain_toggle(')
        self.assertIn('const bool available = lightmap ? hull_lightmap_gain_requested_ : hull_emission_gain_requested_;', toggle)
        self.assertIn('if (lightmap) hull_lightmap_enabled_ = !hull_lightmap_enabled_;', toggle)
        self.assertIn('else hull_gain_enabled_ = !hull_gain_enabled_;', toggle)
        self.assertIn('key=%s accepted=%u enabled=%u requested=%u gain=%g lightmap_requested=%u lightmap_gain=%g hull_enabled=%u lightmap_enabled=%u', toggle)
        self.assertIn('lightmap ? "ctrl_shift_f4" : "ctrl_shift_f6"', toggle)
        self.assertNotIn('CreatePixelShader', toggle)
        self.assertIn('hull_lightmap_frame device=%llu frame=%llu gain=%g fill=%g admitted=%u toggled=%u', motion)
        # The sun-share lane: a gained share variant beside every share variant, selected under the same flag.
        self.assertIn('renderer::linear_material_original_sun_share_pixel_variant(', motion)
        self.assertIn('hull_lightmap_gain_, &gain_applied, lightmap_far_fade_);', motion)
        self.assertIn('sun_shadow_original_lightmap_variant device=%llu original=%016llx transform=%u create=%08lx words=%u depth=%u fill=%g gain=%g share_applied=%u gain_applied=%u', motion)
        self.assertIn('const bool gained_original=original&&hull_lightmap_enabled_&&shadow_.ps_sun_original_lightmap&&hdr_state_==HdrState::Active;', bind)
        self.assertIn('gained_original?shadow_.ps_sun_original_lightmap:original?shadow_.ps_sun_original:', bind)
        self.assertIn('lightmap=gained_original;', bind)
        self.assertIn('IDirect3DPixelShader9* sun_original_lightmap_variant = nullptr;', (ROOT / 'src/proxy/motion_output.h').read_text())
        live = (ROOT / 'verification/probe/run_sun_share_live.py').read_text()
        self.assertIn("'original_lane_lightmap'", live)
        self.assertIn('original_lane_lightmap', (ROOT / 'verification/probe/sun_share_live_inc.h').read_text())
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('IDirect3DPixelShader9* hull_lightmap_variant = nullptr;', header)
        self.assertIn('bool hull_lightmap_pair = false;', header)
        self.assertIn('bool hull_lightmap = false;', header)


if __name__ == '__main__':
    unittest.main()
