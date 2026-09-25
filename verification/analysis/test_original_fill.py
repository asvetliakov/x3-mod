"""Host oracle of the original-shading fill (--original-fill, option C).

Transformer (linear_material_original_fill_pixel_variant): K=0 is byte for
byte the plain motion/depth variant of the original pixel program; K>0 adds
exactly one `def c215 = (K, 1e-22, 2.2, 1/2.2)` before the first declaration
and one 14-instruction block immediately before the located lobe-sum site
(the albedo MUL/MAD of the hull, asteroid, palette and glass families; the
two-armed albedo branch of the XT families), computing
sum.xyz = encode(decode(max(sum,eps)) + K*decode(max(C0,eps))) with the exact
2.2 power law; every original and motion instruction is retained in order
(docs/architecture/original-shading-critique.md 1a, "Implemented"). Launcher
gate: requires --hdr, excludes --linear-materials. No game assets bundled.
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
from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import inspect_motion_output_profiles as shader
import run_linear_material as runner

FILL_CONSTANT = 215
FILLS = (0., 0.03, 0.05)
DEF, MOV, MAD, MAX, POW, IF, IFC = 81, 1, 4, 11, 32, 40, 41
BLOCK_SLOTS = 32
PREREQUISITES = ['--motion-output', '--hdr']
XT_LIGHT0 = 6


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def families():
    """Original program id -> family, from the fixture's 168 reviewed pairs."""
    result = {}
    for index, (vs, ps) in enumerate(runner.PAIRS):
        family = ('hull' if index < 110 else 'asteroid' if index < 116 else 'palette' if index < 148
                  else 'xt' if index < 162 else 'glass')
        result.setdefault(f'vs_{vs}', family)
        result.setdefault(f'ps_{ps}', family)
    return result


def launch(directory, *args):
    spec = importlib.util.spec_from_file_location('original_fill_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    game = Path(directory) / 'game'; game.mkdir(exist_ok=True); (game / 'X3AP.exe').touch()
    wine = Path(directory) / 'wine'; wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
    output, error = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
        try: module.main()
        except SystemExit as exit_error: return exit_error.code, output.getvalue(), error.getvalue()
    return 0, output.getvalue(), error.getvalue()


class OriginalFillTransformerTests(unittest.TestCase):
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
        temporary = tempfile.TemporaryDirectory(prefix='x3-original-fill-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/original_fill_structure.cpp'),
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

    def test_coverage_is_every_reviewed_pixel_program_and_no_vertex_program(self):
        by_family = {}
        for name, family in self.families.items():
            row = self.rows[name]
            stage = name[:2]
            key = (family, stage)
            by_family.setdefault(key, [0, 0])
            by_family[key][0] += 1
            if stage == 'ps':
                self.assertEqual(row['status'], 0, name)
                by_family[key][1] += row['fill_applied']
            else:
                self.assertEqual(row['status'], 3, (name, 'vertex programs carry no fill: UnsupportedShader'))
        self.assertEqual(sum(count for (_, stage), (count, _) in by_family.items()), 137)
        pixel = {family: tuple(counts) for (family, stage), counts in by_family.items() if stage == 'ps'}
        self.assertEqual(pixel, {'hull': (66, 66), 'asteroid': (4, 4), 'palette': (20, 20), 'xt': (14, 14), 'glass': (4, 4)})
        self.assertEqual(sum(count for count, _ in pixel.values()), 108)
        vertex = {family: counts[0] for (family, stage), counts in by_family.items() if stage == 'vs'}
        self.assertEqual(sum(vertex.values()), 29)
        # Outside the 137: nothing else in the corpus is admitted.
        self.assertEqual((self.driver['supported'], self.driver['applied']), (108, 108))
        admitted = {name for name, row in self.rows.items() if row['status'] == 0}
        self.assertEqual(admitted, set(self.covered()))

    def test_zero_fill_is_the_plain_motion_variant_and_retains_the_original(self):
        for name in self.covered():
            original, original_items, _ = shader.instructions((self.originals / f'{name}.bin').read_bytes())
            for depth in (0, 1):
                motion = (self.directory / f'{name}-motion-{depth}.bin').read_bytes()
                self.assertEqual((self.directory / f'{name}-ofill-0-{depth}.bin').read_bytes(), motion, (name, depth))
                words, items, _ = shader.instructions(motion)
                retained = [span(words, item) for item in items if span(words, item) in {span(original, o) for o in original_items}]
                self.assertEqual(retained, [span(original, o) for o in original_items], (name, depth, 'original instructions in order'))

    def block_and_site(self, name, depth, f):
        """(definition, block items, site item, all added spans) of one variant."""
        motion, motion_items, _ = shader.instructions((self.directory / f'{name}-motion-{depth}.bin').read_bytes())
        words, items, _ = shader.instructions((self.directory / f'{name}-ofill-{f}-{depth}.bin').read_bytes())
        motion_spans = [span(motion, m) for m in motion_items]
        added, retained = [], []
        for item in items:
            row = span(words, item)
            if row in motion_spans and motion_spans.count(row) > sum(1 for r in retained if r == row):
                retained.append(row)
            else:
                added.append(item)
        self.assertEqual(retained, motion_spans, (name, depth, f, 'motion program retained in order'))
        self.assertEqual(len(added), 15, (name, depth, f))
        definition, block = added[0], added[1:]
        site = items[items.index(block[-1]) + 1]
        return words, items, definition, block, site

    def test_fill_adds_one_def_and_the_exact_block_before_the_site(self):
        max_site_gap = 0
        for name in self.covered():
            family = self.families[name]
            for depth in (0, 1):
                for f, fill in enumerate(FILLS[1:], 1):
                    words, items, definition, block, site = self.block_and_site(name, depth, f)
                    self.assertEqual(definition['opcode'], DEF, name)
                    self.assertEqual(shader.register_of(definition['words'][0]), (2, FILL_CONSTANT))
                    self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *definition['words'][1:])),
                                     (f32(fill), f32(1e-22), f32(2.2), f32(1.0 / f32(2.2))), name)
                    first_dcl = next(item for item in items if item['opcode'] == shader.DCL)
                    self.assertLess(definition['dword'], first_dcl['dword'], name)
                    # The block is contiguous and immediately precedes the site.
                    self.assertEqual([b['dword'] for b in block[1:]],
                                     [b['dword'] + b['length'] + 1 for b in block[:-1]], name)
                    self.assertEqual(site['dword'], block[-1]['dword'] + block[-1]['length'] + 1, name)
                    opcodes = [b['opcode'] for b in block]
                    self.assertEqual(opcodes, [MAX, POW, POW, POW, MOV, MAX, POW, POW, POW, MAD, MAX, POW, POW, POW], name)
                    # r12 = max(sum, c215.y)
                    dst, src, const = block[0]['words']
                    self.assertEqual((shader.register_of(dst), shader.mask_of(dst)), ((0, 12), 'xyz'))
                    self.assertEqual(shader.register_of(src)[0], 0)
                    sum_register = shader.register_of(src)[1]
                    self.assertEqual(shader.swizzle_of(src), 'xyzw')
                    self.assertEqual((shader.register_of(const), shader.swizzle_of(const)), ((2, FILL_CONSTANT), 'yyyy'))
                    for lane, item in enumerate(block[1:4]):
                        dst, base, exponent = item['words']
                        self.assertEqual((shader.register_of(dst), shader.mask_of(dst)), ((0, 12), 'xyzw'[lane]))
                        self.assertEqual((shader.register_of(base), shader.swizzle_of(base)), ((0, 12), 'xyzw'[lane] * 4))
                        self.assertEqual((shader.register_of(exponent), shader.swizzle_of(exponent)), ((2, FILL_CONSTANT), 'zzzz'))
                    # r13 = decode(max(C0, eps)): the light colour staged alone, then c215.
                    dst, light = block[4]['words']
                    self.assertEqual((shader.register_of(dst), shader.mask_of(dst)), ((0, 13), 'xyz'))
                    self.assertEqual(shader.register_of(light)[0], 2)
                    light_constant = shader.register_of(light)[1]
                    self.assertEqual(shader.swizzle_of(light), 'xyzw')
                    if family == 'xt':
                        self.assertEqual(light_constant, XT_LIGHT0, name)
                    self.assertLess(light_constant, 24, (name, 'a game-declared PS constant (constant-uploads.md: max c23)'))
                    dst, src, const = block[5]['words']
                    self.assertEqual((shader.register_of(dst), shader.register_of(src), shader.swizzle_of(const)), ((0, 13), (0, 13), 'yyyy'))
                    for lane, item in enumerate(block[6:9]):
                        dst, base, exponent = item['words']
                        self.assertEqual((shader.register_of(dst), shader.mask_of(dst)), ((0, 13), 'xyzw'[lane]))
                        self.assertEqual((shader.register_of(base), shader.swizzle_of(base)), ((0, 13), 'xyzw'[lane] * 4))
                        self.assertEqual((shader.register_of(exponent), shader.swizzle_of(exponent)), ((2, FILL_CONSTANT), 'zzzz'))
                    # r12 = r13 * K + r12; r12 = max(r12, eps)
                    dst, a, k, b = block[9]['words']
                    self.assertEqual((shader.register_of(dst), shader.mask_of(dst)), ((0, 12), 'xyz'))
                    self.assertEqual((shader.register_of(a), shader.register_of(k), shader.swizzle_of(k), shader.register_of(b)),
                                     ((0, 13), (2, FILL_CONSTANT), 'xxxx', (0, 12)))
                    dst, src, const = block[10]['words']
                    self.assertEqual((shader.register_of(dst), shader.register_of(src), shader.swizzle_of(const)), ((0, 12), (0, 12), 'yyyy'))
                    # sum.lane = r12.lane ^ (1/2.2): the encode writes the sum register only in xyz.
                    for lane, item in enumerate(block[11:14]):
                        dst, base, exponent = item['words']
                        self.assertEqual((shader.register_of(dst), shader.mask_of(dst)), ((0, sum_register), 'xyzw'[lane]))
                        self.assertEqual((shader.register_of(base), shader.swizzle_of(base)), ((0, 12), 'xyzw'[lane] * 4))
                        self.assertEqual((shader.register_of(exponent), shader.swizzle_of(exponent)), ((2, FILL_CONSTANT), 'wwww'))
                    # No block instruction saturates, uses partial precision, or reads two constants.
                    for item in block:
                        self.assertEqual(item['words'][0] & 0x00f00000, 0, name)
                        self.assertLessEqual(sum(1 for w in item['words'][1:] if shader.register_of(w)[0] == 2), 1, name)
                    # The site: hull-type families multiply the sum register by the albedo
                    # (MUL/MAD into r1 or oC0); XT enters its two-armed albedo branch.
                    if family == 'xt':
                        self.assertIn(site['opcode'], (IF, IFC), name)
                    else:
                        self.assertIn(site['opcode'], (MAD, 5), name)
                        self.assertEqual(shader.register_of(site['words'][1]), (0, sum_register), name)
                    max_site_gap = max(max_site_gap, 0)
                    # Original constants: none of the retained instructions defines or reads c215.
                    for item in items:
                        if item in block or item is definition:
                            continue
                        registers = [shader.register_of(w) for w in item['words']]
                        self.assertNotIn((2, FILL_CONSTANT), registers, (name, 'c215 collision'))

    def test_slot_budget_and_constant_reservation(self):
        for name in self.covered():
            row = self.rows[name]
            for depth in (0, 1):
                self.assertEqual(row['variant_slots'][depth] - row['motion_slots'][depth], BLOCK_SLOTS, name)
                self.assertEqual(row['variant_instructions'][depth] - row['motion_instructions'][depth], 14, name)
                self.assertLessEqual(row['variant_slots'][depth], 512, name)
        self.assertEqual(self.driver['max_variant_slots'], max(max(self.rows[n]['variant_slots']) for n in self.covered()))
        self.assertLess(self.driver['max_variant_slots'], 512)
        # c215 sits above every game-declared PS constant (c23), outside the
        # linear-material DEFs (c212-c213), the fade producer (c214) and the
        # motion ABI (c216-c220); the same register the converted route's fill uses.
        self.assertGreater(FILL_CONSTANT, 23)
        self.assertNotIn(FILL_CONSTANT, shader.PIXEL_ABI_CONSTANTS)
        self.assertNotIn(FILL_CONSTANT, (212, 213, 214))
        source = (ROOT / 'src/renderer/linear_material.cpp').read_text()
        self.assertIn('constexpr unsigned fill_constant = 215;', source)
        self.assertIn('emit(out,def,{dst(constant,fill_constant,xyzw),bits(fill),bits(original_fill_epsilon),bits(2.2f),bits(1.0f/2.2f)});', source)


class LauncherGateTests(unittest.TestCase):
    def test_default_off_requires_hdr(self):
        # The launcher's --linear-materials / --material-fill went on 2026-09-25 (docs/verification/launcher-options-inventory.md,
        # "Removed 2026-09-25"); the DLL still excludes the fill under X3M_LINEAR_MATERIALS (test below).
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
            baseline = json.loads(output)['env']
            self.assertEqual(baseline['X3M_ORIGINAL_FILL'], '0.0')
            self.assertNotIn('X3M_LINEAR_MATERIALS', baseline); self.assertNotIn('X3M_MATERIAL_FILL', baseline)
            code, output, error = launch(directory, *PREREQUISITES, '--original-fill', '0.05'); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_ORIGINAL_FILL'], '0.05')
            # An explicit value also carries the launcher's marker 0 (test_original_fill_default); nothing else changes.
            self.assertEqual(env['X3M_ORIGINAL_FILL_DEFAULT'], '0')
            self.assertNotIn('X3M_ORIGINAL_FILL_DEFAULT', baseline)
            fill_keys = ('X3M_ORIGINAL_FILL', 'X3M_ORIGINAL_FILL_DEFAULT')
            self.assertEqual({k: v for k, v in env.items() if k not in fill_keys},
                             {k: v for k, v in baseline.items() if k not in fill_keys})
            for bad in (('--motion-output', '--original-fill', '0.05'),
                        (*PREREQUISITES, '--original-fill', '-0.01'), (*PREREQUISITES, '--original-fill', '0.51'),
                        (*PREREQUISITES, '--original-fill', 'nan'), (*PREREQUISITES, '--original-fill', 'inf')):
                code, _, error = launch(directory, *bad); self.assertEqual(code, 2, bad); self.assertIn('--original-fill', error)
            for boundary in ('0', '0.5'):
                code, output, error = launch(directory, *PREREQUISITES, '--original-fill', boundary); self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_ORIGINAL_FILL'], repr(float(boundary)))

    def test_dll_gate_reads_the_variable_and_needs_hdr_without_linear_materials(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = source[source.index('X3M_ORIGINAL_FILL=<k>'):][:2600]
        self.assertIn('GetEnvironmentVariableW(L"X3M_ORIGINAL_FILL",setting,32)', block)
        self.assertIn('value>=0.f&&value<=.5f', block)
        self.assertIn('const bool excluded=linear_material_requested;', block)
        self.assertIn('if(!hdr_requested||excluded)original_fill=0.f;', block)
        self.assertIn('configure_original_fill(original_fill)', source)
        for absent in ('taa_requested', 'screen_ownership'):
            self.assertNotIn(absent, block)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('original_fill_requested_ = std::isfinite(fill) && fill > 0.f && fill <= .5f && !linear_material_requested_;', motion)
        # Created once at registration beside the motion variant; selected in
        # the one bind pair of the routed draw; undone with the route.
        self.assertIn('renderer::linear_material_original_fill_pixel_variant(', motion)
        self.assertIn('original_fill_variant device=%llu original=%016llx transform=%u create=%08lx words=%u depth=%u fill=%g fill_applied=%u', motion)
        # The whole bind method: the fill variant is selected in the one bind
        # pair and recorded on the route only once the bind succeeded.
        bind = extract_function(motion, 'HRESULT MotionOutput::bind_variant_pair(')
        select = bind.index('shadow_.original_fill_pair && !material && !shadow_.xt_default_ready && hdr_state_ == HdrState::Active')
        self.assertIn('ps = shadow_.ps_original_fill_variant; fill = true;', bind)
        record = bind.index('route.original_fill = true')
        self.assertLess(select, record)
        self.assertIn('if (fill && SUCCEEDED(hr)) route.original_fill = true', bind)
        self.assertNotIn('GetRenderState', bind)
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('IDirect3DPixelShader9* original_fill_variant = nullptr;', header)
        self.assertIn('bool original_fill_pair = false;', header)
        contract = motion[motion.index('void MotionOutput::refresh_linear_material_contract'):][:2400]
        self.assertIn('shadow_.original_fill_pair = original_fill_requested_ && shadow_.ps_original_fill_variant && shadow_.vs_variant', contract)
        self.assertIn('renderer::linear_material_pair_reviewed(shadow_.vs_hash, shadow_.ps_hash)', contract)


if __name__ == '__main__':
    unittest.main()
