"""Host oracle of the hull emissive widening (--hull-emissive-widening K,Q0,Q1;
docs/architecture/hull-emissive-widening.md).

Transformer (linear_material_hull_lightmap_gain_pixel_variant / the share entry
point with widen=true): the gained variant with its light-map `texld rL, v1, s`
replaced by `dsx rG.xy, v1 / dsy rG.zw, v1.xyxy / mul rG, rG, c217.z /
texldd rL, v1, s, rG.xy, rG.zw`, rG one above the program's highest temporary,
nothing else changed (+12 DWORDs, +3 instructions, +7 weighted slots). The
byte-exact Python reference below rebuilds every widened variant from the
gained one and compares. The coverage test walks the local archive manifest:
every ps_3_0 program declaring LightMapTexSampler is widened or in the explicit
exclusion list (the moon), and the hash -> family -> stage -> transformed table
is written as tracked JSON. Launcher and DLL plumbing are checked by source
contract. No game assets bundled (the corpus is local, X3M_SHADER_PROGRAM_DIRECTORY).
"""
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
from shader_constants import parse_ctab
from verification.analysis.test_capture_bloom_lifetime import extract_function
from verification.analysis.test_original_fill import families, launch
from verification.analysis.test_hull_lightmap_gain import FILLS, UNTOUCHED

DSX, DSY, TEXLDD, TEXLD, MUL, DEF, DCL = 91, 92, 93, 66, 5, 81, 31
PREREQUISITES = ['--motion-output', '--hdr']
COVERAGE = ROOT / 'verification/results/hull-emissive-widening-coverage.json'
# SM3 light-map programs outside the reviewed originals, by design (hull-emissive-widening.md 2.5).
EXCLUDED = {'ps_6aaaa2cb27e92cc8': 'moon (shader/3_0/moon_0000.fb, moon_0001.fb: coordinate v2, no motion row)'}
LIGHTMAP_SAMPLER = 'LightMapTexSampler'
WIDEN_SLOTS, WIDEN_WORDS, WIDEN_INSTRUCTIONS = 7, 12, 3


def swizzle(x, y, z, w):
    return (x | y << 2 | z << 4 | w << 6) << 16


def src(reg, sw=(0, 1, 2, 3)):
    return 0x80000000 | reg | swizzle(*sw)


def dst(reg, mask):
    return 0x80000000 | reg | mask << 16


def expected_dynamic(words):
    """The far-fade form of a static gained variant: no `def c223`, the MUL reads c217.w."""
    out, dropped, replaced = [words[0]], False, 0
    i = 1
    while i < len(words):
        if not dropped and i + 5 < len(words) and words[i] == 0x05000051 and words[i + 1] & 0x70000000 == 0x20000000 and words[i + 1] & 0x7ff == 223:
            dropped = True; i += 6; continue
        if words[i] == 0xa00000df:
            out.append(0xa0ff00d9); replaced += 1
        else:
            out.append(words[i])
        i += 1
    assert dropped and replaced == 1
    return out


def reference_widened(gained):
    """The widened variant of a gained (dynamic) variant: the last texld replaced by the 16-word block, rG = max temp + 1."""
    words, items, _ = shader.instructions(struct.pack('<%dI' % len(gained), *gained))
    fetch = [item for item in items if item['opcode'] == TEXLD][-1]
    highest = -1
    for item in items:
        if item['opcode'] in (DCL, DEF):
            continue
        for word in item['words']:
            kind, number = shader.register_of(word)
            if kind == 0:
                highest = max(highest, number)
    g = highest + 1
    assert g < 32
    destination, coordinate, sampler = fetch['words']
    assert shader.register_of(coordinate)[0] == 1 and shader.swizzle_of(coordinate) == 'xyzw' and coordinate >> 24 & 0xf == 0
    block = [2 << 24 | DSX, dst(g, 3), coordinate,
             2 << 24 | DSY, dst(g, 12), (coordinate & ~0xff0000) | swizzle(0, 1, 0, 1),
             3 << 24 | MUL, dst(g, 15), src(g), 0xa0aa00d9,  # c217.z
             5 << 24 | TEXLDD, destination, coordinate, sampler, src(g, (0, 1, 1, 1)), src(g, (2, 3, 3, 3))]
    at = fetch['dword']
    return list(words[:at]) + block + list(words[at + 4:]), g


def program_words(path):
    data = path.read_bytes()
    return list(struct.unpack('<%dI' % (len(data) // 4), data))


class HullEmissiveWideningTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        cls.families = families()
        if not all((cls.originals / f'{name}.bin').is_file() for name in cls.families):
            reason = f'local original corpus unavailable under {cls.originals} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            print('SKIP:', reason, file=sys.stderr)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-hull-widen-')
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

    def gained(self):
        return sorted(name for name, family in self.families.items() if name.startswith('ps_') and name not in UNTOUCHED)

    def test_python_reference_rebuilds_every_widened_variant_byte_for_byte(self):
        registers, checks = {}, 0
        for name in self.gained():
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    gained = expected_dynamic(program_words(self.directory / f'{name}-hlgain-{f}-{depth}.bin'))
                    widened = program_words(self.directory / f'{name}-hlwiden-{f}-{depth}.bin')
                    expected, g = reference_widened(gained)
                    self.assertEqual(widened, expected, (name, f, depth))
                    registers.setdefault(g, 0); registers[g] += 1
                    checks += 1
                share = expected_dynamic(program_words(self.directory / f'{name}-hlsharegain-{f}.bin'))
                widened = program_words(self.directory / f'{name}-hlsharewiden-{f}.bin')
                expected, g = reference_widened(share)
                self.assertEqual(widened, expected, (name, f, 'share'))
                registers.setdefault(g, 0); registers[g] += 1
                checks += 1
        self.assertEqual(checks, 100 * 6)
        # rG sits above every register: the plain variants' motion bodies end at r8/r9 (XT r9/r10),
        # the share variants at r23 (its r11-r23 reduction), never at or beyond r32.
        self.assertTrue(all(g < 32 for g in registers), registers)
        self.assertEqual(sum(registers.values()), 600)
        self.assertEqual(max(registers), 24)

    def test_untouched_programs_and_slot_growth(self):
        for name in sorted(UNTOUCHED):
            self.assertEqual(self.rows[name]['widen_applied'], 0, name)
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    base = (self.directory / f'{name}-hlbase-{f}-{depth}.bin').read_bytes()
                    self.assertEqual((self.directory / f'{name}-hlwiden-{f}-{depth}.bin').read_bytes(), base, (name, f, depth))
                self.assertEqual((self.directory / f'{name}-hlsharewiden-{f}.bin').read_bytes(), (self.directory / f'{name}-hlshare-{f}.bin').read_bytes(), name)
        growth = {}
        for name in self.gained():
            row = self.rows[name]
            self.assertEqual(row['widen_applied'], 1, name)
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    self.assertEqual(row['widen_slots'][f][depth] - row['variant_slots'][f][depth], WIDEN_SLOTS, name)
                    self.assertLessEqual(row['widen_slots'][f][depth], 512, name)
                self.assertEqual(row['share_widen_slots'][f] - row['share_variant_slots'][f], WIDEN_SLOTS, name)
                self.assertLessEqual(row['share_widen_slots'][f], 512, name)
            family = growth.setdefault(self.families[name], dict(programs=0, max_widen_slots=0, max_share_widen_slots=0))
            family['programs'] += 1
            family['max_widen_slots'] = max(family['max_widen_slots'], max(max(v) for v in row['widen_slots']))
            family['max_share_widen_slots'] = max(family['max_share_widen_slots'], max(row['share_widen_slots']))
        self.assertEqual({k: v['programs'] for k, v in growth.items()}, {'hull': 66, 'palette': 20, 'xt': 14})
        self.assertEqual(self.driver['max_widen_slots'], self.driver['max_variant_slots'] + WIDEN_SLOTS)
        self.assertLess(self.driver['max_widen_slots'], 512)

    def test_widened_block_shape(self):
        # dsx/dsy/mul/texldd in place of the texld, the gain MUL right after, c217.z read exactly once, no DEF of c217/c223.
        for name in self.gained():
            words, items, _ = shader.instructions((self.directory / f'{name}-hlwiden-0-1.bin').read_bytes())
            self.assertEqual([item['opcode'] for item in items if item['opcode'] == TEXLD][-1:], [TEXLD], name)  # earlier texture reads stay
            texldd = [item for item in items if item['opcode'] == TEXLDD]
            self.assertEqual(len(texldd), 1, name)
            at = items.index(texldd[0])
            self.assertEqual([items[at - 3]['opcode'], items[at - 2]['opcode'], items[at - 1]['opcode'], items[at + 1]['opcode']], [DSX, DSY, MUL, MUL], name)
            self.assertEqual(shader.swizzle_of(items[at - 1]['words'][2]), 'zzzz')
            self.assertEqual(shader.register_of(items[at - 1]['words'][2]), (2, 217))
            self.assertEqual(shader.register_of(items[at + 1]['words'][2]), (2, 217))
            self.assertEqual(shader.swizzle_of(items[at + 1]['words'][2]), 'wwww')
            self.assertGreater(items.index([item for item in items if item['opcode'] == TEXLD][-1]), 0)
            self.assertLess([item for item in items if item['opcode'] == TEXLD][-1]['dword'], texldd[0]['dword'], (name, 'the light-map fetch is the last texture read'))
            z_reads = sum(1 for item in items if item['opcode'] not in (DCL, DEF) for w in item['words'][1:] if shader.register_of(w) == (2, 217) and shader.swizzle_of(w) == 'zzzz')
            self.assertEqual(z_reads, 1, name)
            self.assertFalse(any(item['opcode'] == DEF and shader.register_of(item['words'][0]) in ((2, 217), (2, 223)) for item in items), name)
            # dsx/dsy source the coordinate register directly; texldd keeps the texld's destination word (mask and _pp).
            self.assertEqual(shader.register_of(items[at - 3]['words'][1])[0], 1, name)
            self.assertEqual(items[at - 3]['words'][1], texldd[0]['words'][1], name)
            self.assertEqual(shader.mask_of(texldd[0]['words'][0]), 'xyzw', name)

    def test_coverage_every_sm3_lightmap_program_is_widened_or_listed(self):
        manifest_path = self.originals.parent / 'manifest.json'
        if not manifest_path.is_file():
            self.skipTest(f'archive manifest missing beside the corpus: {manifest_path}')
        manifest = json.loads(manifest_path.read_text())
        effects = {}
        for effect in manifest['effects']:
            path = effect['path']
            if not path.startswith('shader/3_0/'):
                continue  # every 3_0 directory (hueshift_off, hue_lights_off included): the family is the effect's base name
            name = path.split('/')[-1].split('.')[0]
            for suffix in ('_0000', '_0001', '2s'):
                name = name.removesuffix(suffix) if hasattr(name, 'removesuffix') else (name[:-len(suffix)] if name.endswith(suffix) else name)
            for program in effect['program_occurrences']:
                effects.setdefault(program, set()).add(name)
        selected = {f'ps_{ps}' for _, ps in runner.PAIRS}
        rows, untransformed = [], []
        for program in manifest['programs']:
            if program['stage'] != 'ps' or program['model'] != '3_0':
                continue
            code = (self.originals / f"{program['id']}.bin").read_bytes()
            samplers = [c for c in parse_ctab(code) if c['name'] == LIGHTMAP_SAMPLER]
            if not samplers:
                continue
            name = program['id']
            transformed = name in selected and self.rows.get(name, {}).get('widen_applied') == 1
            row = dict(hash=name[3:], family=sorted(effects.get(name, ())), group=self.families.get(name), stage=samplers[0]['register'],
                       words=program['bytes'] // 4, transformed=transformed)
            if transformed:
                driver = self.rows[name]
                row['widened_slots'] = dict(plain=max(max(v) for v in driver['widen_slots']), share=max(driver['share_widen_slots']))
            elif name in EXCLUDED:
                row['excluded'] = EXCLUDED[name]
            else:
                untransformed.append(name)
            rows.append(row)
        rows.sort(key=lambda r: r['hash'])
        self.assertEqual(untransformed, [], 'SM3 light-map programs neither widened nor listed')
        self.assertEqual(len(rows), 101)
        self.assertEqual(sum(r['transformed'] for r in rows), 100)
        self.assertEqual({r['stage'] for r in rows if r['transformed']}, {2, 3})
        self.assertEqual(sum(1 for r in rows if r['transformed'] and r['stage'] == 2), 44)
        by_family = {}
        for r in rows:
            if r['transformed']:
                key = '+'.join(r['family']) or r['group']
                entry = by_family.setdefault(key, dict(programs=0, max_plain_slots=0, max_share_slots=0))
                entry['programs'] += 1
                entry['max_plain_slots'] = max(entry['max_plain_slots'], r['widened_slots']['plain'])
                entry['max_share_slots'] = max(entry['max_share_slots'], r['widened_slots']['share'])
        COVERAGE.write_text(json.dumps(dict(
            description='Every ps_3_0 program of the local archive declaring LightMapTexSampler: widened (hull emissive widening) or excluded by design. '
                        'Slots are the weighted SM3 totals of the widened variants (+7 over the gained ones); the moon program is out of scope.',
            source_manifest_sha256=manifest.get('index_sha256'), programs=len(rows), transformed=sum(r['transformed'] for r in rows),
            excluded={k[3:]: v for k, v in EXCLUDED.items()}, slot_growth_per_variant=WIDEN_SLOTS, per_family=by_family, rows=rows), indent=1) + '\n')


class LauncherAndProxyTests(unittest.TestCase):
    def test_launcher_option(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
            baseline = json.loads(output)['env']
            self.assertNotIn('X3M_HULL_EMISSIVE_WIDENING', baseline)  # default off (first flight)
            with mock.patch.dict(os.environ, {'X3M_HULL_EMISSIVE_WIDENING': '3,2,8'}):
                code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env'], baseline)  # an inherited value is dropped
            for value, expected in (('3,2,8', '3,2,8'), ('2,0,10', '2,0,10'), ('8,0.5,1e6', '8,0.5,1e+06'), ('1.5,2,8', '1.5,2,8')):
                code, output, error = launch(directory, *PREREQUISITES, '--hull-emissive-widening', value); self.assertEqual(code, 0, error)
                env = json.loads(output)['env']
                self.assertEqual(env.pop('X3M_HULL_EMISSIVE_WIDENING'), expected)
                self.assertEqual(env, baseline)
            for value in ('', '3', '3,2', '3,2,8,1', '1,2,8', '0.5,2,8', '8.5,2,8', '3,8,2', '3,2,2', '3,-1,8', '3,2,2e6', 'a,b,c', '3,nan,8', 'inf,2,8'):
                code, _, error = launch(directory, *PREREQUISITES, '--hull-emissive-widening', value)
                self.assertEqual(code, 2, value); self.assertIn('--hull-emissive-widening', error)
            for extra in (['--hull-lightmap-gain', '1'], ['--linear-materials', '--hdr-tonemap']):
                code, _, error = launch(directory, *PREREQUISITES, *extra, '--hull-emissive-widening', '3,2,8')
                self.assertEqual(code, 2, extra); self.assertIn('--hull-emissive-widening', error)
            code, _, error = launch(directory, '--motion-output', '--hull-emissive-widening', '3,2,8'); self.assertEqual(code, 2)
            # No --taa requirement: the option latches the camera itself, as the far fade does.
            code, output, error = launch(directory, *PREREQUISITES, '--no-light-map-far-fade', '--hull-emissive-widening', '3,2,8'); self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_HULL_EMISSIVE_WIDENING'], '3,2,8')

    def test_dll_plumbing(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = capture[capture.index('// X3M_HULL_EMISSIVE_WIDENING=K,Q0,Q1 (docs/architecture'):][:2600]
        self.assertIn('GetEnvironmentVariableW(L"X3M_HULL_EMISSIVE_WIDENING",widen_setting,96)', block)
        self.assertIn('valid=valid&&count==3&&parsed[0]>1.f&&parsed[0]<=8.f&&parsed[1]>=0.f&&parsed[2]>parsed[1]&&parsed[2]<=1e6f;', block)
        self.assertIn('hull_emissive_widening_requested=valid&&gained;', block)
        self.assertIn('camera_state::request_consumer();', block)
        self.assertIn('hull_emissive_widening_mode requested=1 enabled=%u valid=%u k=%g q0=%g q1=%g gain=%g%s', block)
        self.assertIn('configure_hull_emissive_widening(hull_emissive_widening[0],hull_emissive_widening[1],hull_emissive_widening[2])', capture)
        self.assertIn('hull_emissive_widening_configured accepted=%u k=%g q0=%g q1=%g', capture)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        configure = extract_function(motion, 'bool MotionOutput::configure_hull_emissive_widening(')
        self.assertIn('if (device_) return lightmap_widen_;', configure)
        self.assertIn('lightmap_widen_ = hull_lightmap_gain_requested_ && std::isfinite(k) && std::isfinite(q0) && std::isfinite(q1)', configure)
        self.assertIn('&& k > 1.f && k <= 8.f && q0 >= 0.f && q1 > q0 && q1 <= 1e6f;', configure)
        # The per-draw k rides the motion ABI's own two-vector upload (c217.z): no extra constant write, no allocation.
        draw = extract_function(motion, 'void MotionOutput::evaluate_draw(')
        self.assertIn('previous_rows ? 1.f : 0.f, 0.f, lightmap_widen_draw_k_, lightmap_fade_gain_};', draw)
        self.assertEqual(draw.count('SetPixelShaderConstantF'), 1)
        self.assertIn('lightmap_widen_draw_k_ = lightmap_widen_ && gain_pair', draw)
        self.assertIn('fade_route::lightmap_widen_scale(rows[15], lightmap_fade_m00_ > 0.f, lightmap_fade_m00_, float(target_width_),', draw)
        core = (ROOT / 'src/proxy/fade_route_core.h').read_text()
        law = extract_function(core, 'inline float lightmap_widen_scale(')
        for text in ('if (!camera_valid || !(m00 > 0.f) || !(width > 0.f) || !(w > 0.f)) return 1.f;', 'if (!(t > 0.f)) return 1.f;',
                     'if (t >= 1.f) return k;', 'return 1.f + (k - 1.f) * t;'):
            self.assertIn(text, law)
        # Selection: the widened variant only over a selected gain variant, k > 1, not alpha tested, a mip chain on the stage; no getter.
        bind = extract_function(motion, 'HRESULT MotionOutput::bind_variant_pair(')
        self.assertIn('const bool widen_draw = (lightmap_widen_draw_k_ > 1.f || widen_forced) && !route.alpha_tested && shadow_.hull_lightmap_stage', bind)
        self.assertIn('&& samplers_[shadow_.hull_lightmap_stage].levels > 1;', bind)
        self.assertIn('if (widen_draw && shadow_.ps_hull_lightmap_widen) { ps = shadow_.ps_hull_lightmap_widen; widen = true; }', bind)
        self.assertIn('const bool widened_original=gained_original&&widen_draw&&shadow_.ps_sun_original_lightmap_widen;', bind)
        self.assertIn('if (widen && SUCCEEDED(hr)) route.hull_lightmap_widen = true;', bind)
        self.assertNotIn('GetRenderState', bind); self.assertNotIn('GetTexture', bind)
        # Created beside the gained variant only (k = 1 falls back to it), both lanes, with the stage recorded.
        self.assertEqual(motion.count('true, &widen_applied);'), 2)
        self.assertIn('if (lightmap_widen_ && entry.hull_lightmap_variant) {', motion)
        self.assertIn('if (lightmap_widen_ && entry.sun_original_lightmap_variant) {', motion)
        self.assertIn('hull_lightmap_widen_variant device=%llu original=%016llx transform=%u create=%08lx words=%u depth=%u k=%g stage=%u widen_applied=%u', motion)
        self.assertIn('renderer::linear_material_hull_lightmap_stage(hash, bytes / 4)', motion)
        # Level counts are read for the widening as for the mip bias; the camera latch serves both; frame and session lines.
        self.assertIn('return (mip_bias_bits_ || lightmap_widen_) && texture && stage < sampler_stage_count && samplers_[stage].texture != texture;', motion)
        self.assertIn('if ((lightmap_far_fade_ || lightmap_widen_) && scene) { camera_state::Sample sample{}; lightmap_fade_m00_ = camera_state::read(&sample) ? sample.state.m00 : 0.f; }', motion)
        self.assertIn('hull_lightmap_widen_frame device=%llu frame=%llu admitted=%u widened=%u unity=%u k_min=%g k_max=%g camera=%u', motion)
        self.assertIn('hull_lightmap_widen_summary device=%llu k=%g q0=%g q1=%g variants=%lu widened_draws=%lu k_min=%g k_max=%g', motion)
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        for text in ('IDirect3DPixelShader9* hull_lightmap_widen_variant = nullptr;', 'IDirect3DPixelShader9* sun_original_lightmap_widen_variant = nullptr;',
                     'IDirect3DPixelShader9* ps_hull_lightmap_widen = nullptr;', 'std::uint8_t hull_lightmap_stage = 0;', 'bool hull_lightmap_widen = false;'):
            self.assertIn(text, header)
        source = (ROOT / 'src/renderer/linear_material.cpp').read_text()
        self.assertIn('constexpr unsigned dsx = 91, dsy = 92, texldd = 93;', source)
        self.assertIn('case dsx: case dsy: operands=2; slots=2; return true;', source)
        self.assertIn('case texldd: operands=5; slots=3; return true;', source)
        self.assertIn('if (op==texldd) { if (dimension!=2) return false; }', source)
        self.assertIn('if (widen && lightmap_gain==1.0f) return LinearMaterialResult::InvalidConfig;', source)
        self.assertIn("'dsx': 2, 'dsy': 2, 'texldd': 3", (ROOT / 'tools/analysis/inspect_motion_output_profiles.py').read_text())


if __name__ == '__main__':
    unittest.main()
