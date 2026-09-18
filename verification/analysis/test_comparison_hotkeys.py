"""Focused host execution: no Wine, game, shader compiler or DLL build."""
import argparse
import ast
import contextlib
import io
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from unittest import mock

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class ComparisonHotkeys(unittest.TestCase):
    def test_production_controls_and_exposure_handoff(self):
        source = (ROOT / 'src/renderer/hdr_pass.cpp').read_text()
        functions = [extract_function(source, signature) for signature in (
            'void HdrPass::prepare_constants(', 'bool HdrPass::comparison_exposure(')]
        self.run_host('comparison_controls_fixture.cpp', functions, exposure=True)

    def test_production_notice_state_failure_and_allocation_contract(self):
        self.run_host('comparison_notice_fixture.cpp', [], notice=True)

    def test_production_bloom_handoff_preserves_display_and_off_filtering(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        functions = [extract_function(source, 'void retain_compositor_scene(')]
        self.run_host('comparison_bloom_handoff_fixture.cpp', functions, handoff=True)

    def test_sun_shadow_at_rest_key_and_scene_end_gate(self):
        """Ctrl+Shift+F12 (comparison-hotkeys.md, "Sun shadows at rest"): the
        edge-triggered key, polled only with --sun-shadow-apply, and the one
        boolean that removes the replay transaction and the apply quad from the
        scene end while everything else keeps running."""
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        controls = (ROOT / 'src/proxy/comparison_controls.h').read_text()
        motion_source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        replay = (ROOT / 'src/proxy/motion_output_shadow_replay_inc.h').read_text()
        # The key: its own latch and edge in the shared sampler.
        self.assertIn('bool sun_shadow = false;', controls)
        self.assertIn('result.sun_shadow = keys.sun_shadow && !sun_shadow_down_;', controls)
        self.assertIn('sun_shadow_down_ = keys.sun_shadow;', controls)
        polling = extract_function(capture, 'void comparison_begin_frame(')
        self.assertIn('keys.sun_shadow=sun_shadow_apply_requested && (GetAsyncKeyState(VK_F12)&0x8000)!=0;', polling)
        self.assertEqual(capture.count('GetAsyncKeyState(VK_F12)'), 1, 'one owner per function key')
        self.assertIn('if(action.sun_shadow)ctx.motion_output.sun_shadow_toggle();', polling)
        self.assertIn('!emitter_compare && !sun_shadow_apply_requested && !fps_overlay_requested)return;', polling)
        self.assertLess(polling.index('!fps_overlay_requested)return;'), polling.index('comparison_foreground()'))
        # The toggle: no allocation, no device object, every retained basis voided.
        toggle = extract_function(motion_source, 'int MotionOutput::sun_shadow_toggle(')
        self.assertIn('sun_shadow_enabled_ = !sun_shadow_enabled_;', toggle)
        self.assertIn('if (depth_replay_) depth_replay_->invalidate_retained();', toggle)
        self.assertIn('sun_shadow_toggle device=%llu state=%u frame=%llu', toggle)
        # A device with neither the replay nor the apply: a logged no-op, as an
        # unrequested ambient-occlusion press is.
        self.assertIn('if (!sun_apply_requested_ && !depth_replay_requested_) {', toggle)
        self.assertIn('return -1;', toggle)
        self.assertLess(toggle.index('return -1;'), toggle.index('sun_shadow_enabled_ = !sun_shadow_enabled_;'))
        # The single map's publication goes with the retained cascade bases,
        # and the frame back on replays every cascade whatever the budget says.
        self.assertIn('depth_basis_ = {};', toggle)
        self.assertIn('sun_shadow_force_replay_ = sun_shadow_enabled_;', toggle)
        self.assertIn('view_rows_valid_ = false; }', (ROOT / 'src/renderer/shadow_replay_pass.h').read_text())
        cascades = extract_function(replay, 'void MotionOutput::run_shadow_replay_cascades(')
        self.assertIn('(sun_shadow_force_replay_ || renderer::shadow_cascade_replays(', cascades)
        self.assertIn('sun_shadow_force_replay_ = false;', cascades)
        # An F8 while off dumps no stale map (the basis line still reports valid=0).
        dump = extract_function(motion_source, 'void MotionOutput::readback(')
        self.assertIn('if (rows && depth_replayed_frame_ == frame_ && depth_replayed_)', dump)
        for absent in ('CreateTexture', 'execute', 'Clear'):
            self.assertNotIn(absent, toggle)
        # The scene end: one boolean test, both the replay transaction and the
        # apply quad, with the frame's leases still retired.
        scene_end = extract_function(motion_source, 'void MotionOutput::publish_shadow_replay_candidates(')
        self.assertIn('if (!sun_shadow_enabled_) {', scene_end)
        self.assertIn('if (depth_replay_requested_) release_depth_leases();', scene_end)
        self.assertIn('else if (depth_cascades_on()) run_shadow_replay_cascades(quiet_records);', scene_end)
        self.assertEqual(motion_source.count('run_sun_shadow_apply();'), 2)
        self.assertEqual(motion_source.count('sun_apply_requested_&&sun_shadow_enabled_)run_sun_shadow_apply();')
                         + motion_source.count('sun_apply_requested_ && sun_shadow_enabled_) run_sun_shadow_apply();'), 2)
        # The per-frame shadow line carries the state it ran under.
        self.assertEqual(replay.count('us=%.1f shadow_toggle=%u'), 2)  # the single map and the cascades
        self.assertEqual(replay.count('us=%.1f shadow_toggle=%u%s far_replayed='), 1)

    def run_host(self, fixture, functions, exposure=False, notice=False, handoff=False):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='x3-comparison-') as temporary:
            directory = Path(temporary)
            (directory / ('comparison_handoff_under_test_inc.h' if handoff else 'comparison_exposure_under_test_inc.h')).write_text('\n'.join(functions))
            stub = ROOT / 'verification/probe/hdr_display_snapshot_stubs/d3d9.h'
            (directory / 'd3d9.h').write_text(f'#include "{stub}"\n' + textwrap.dedent('''
                #define WINAPI
                using LONG=std::int32_t;
                using D3DCOLOR=DWORD;
                struct D3DRECT { LONG x1,y1,x2,y2; };
                struct D3DVIEWPORT9 { DWORD X,Y,Width,Height; float MinZ,MaxZ; };
                enum D3DBACKBUFFER_TYPE { D3DBACKBUFFER_TYPE_MONO };
                constexpr HRESULT D3DERR_NOTFOUND=-99;
                constexpr DWORD D3DCLEAR_TARGET=1;
            '''))
            extra = []
            if exposure:
                extra.append(str(ROOT / 'src/renderer/exposure.cpp'))
            if notice:
                extra.append(str(ROOT / 'src/proxy/comparison_notice.cpp'))
            for name, flags in [('release', ['-O2']), ('sanitized', ['-O1', '-g', '-fsanitize=address,undefined'])]:
                with self.subTest(fixture=fixture, mode=name):
                    executable = directory / name
                    result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I', str(directory), str(ROOT / 'verification/probe' / fixture), *extra,
                        '-o', str(executable)], capture_output=True, text=True, timeout=60)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=20)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('failures=0', result.stdout)
                    print(name, result.stdout.strip())

    def test_launcher_auto_default_fixed_and_manual_override(self):
        # Execute the real parser/validation AST and exact exposure environment
        # assignments. Stop before filesystem/launch/install handling.
        source = (ROOT / 'tools/manage.py').read_text()
        tree = ast.parse(source)
        main = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'main')
        statements = []
        for node in main.body:
            if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id == 'game':
                break
            statements.append(node)
        assignments = [node for node in ast.walk(main) if isinstance(node, ast.Assign)
            and isinstance(node.targets[0], ast.Subscript)
            and isinstance(node.targets[0].value, ast.Name) and node.targets[0].value.id == 'env'
            and isinstance(node.targets[0].slice, ast.Constant)
            and node.targets[0].slice.value in ('X3M_HDR_EXPOSURE', 'X3M_HDR_EV_MANUAL', 'X3M_HDR_EV_MAX')]
        main.body = statements + assignments + [ast.Return(value=ast.Name(id='env', ctx=ast.Load()))]
        compiled = compile(ast.fix_missing_locations(ast.Module(body=[main], type_ignores=[])), 'manage_under_test.py', 'exec')
        scope = dict(argparse=argparse, Path=Path, GAME=Path('/unused'), BOTTLE='X3', ROOT=ROOT, __doc__='test')
        exec(compiled, scope)
        base = ['manage.py', 'launch', '--motion-output', '--hdr', '--hdr-tonemap']
        cases = [([], 'auto', ''), (['--hdr-exposure', 'fixed'], 'fixed', ''),
                 (['--hdr-exposure', 'auto'], 'auto', ''),
                 (['--hdr-ev-manual', '-1'], 'fixed', '-1.0'),
                 (['--hdr-exposure', 'auto', '--hdr-ev-manual', '0.5'], 'fixed', '0.5')]
        for arguments, policy, manual in cases:
            with self.subTest(arguments=arguments), mock.patch.object(sys, 'argv', base + arguments):
                scope['env'] = {'X3M_HDR_EXPOSURE': 'fixed', 'X3M_HDR_EV_MANUAL': '2', 'X3M_HDR_EV_MAX': '2.0'}
                result = scope['main']()
                self.assertEqual(result, {'X3M_HDR_EXPOSURE': policy, 'X3M_HDR_EV_MANUAL': manual, 'X3M_HDR_EV_MAX': '1.3'})
        with mock.patch.object(sys, 'argv', base + ['--hdr-ev-max', '1.5']):
            scope['env'] = {}
            self.assertEqual(scope['main']()['X3M_HDR_EV_MAX'], '1.5')
        for policy in ('fixed', 'auto'):
            with mock.patch.object(sys, 'argv', ['manage.py', 'launch', '--hdr-exposure', policy]), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    scope['main']()
                self.assertEqual(error.exception.code, 2)

    def test_frame_state_and_capability_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        present = extract_function(capture, 'HRESULT WINAPI present(')
        self.assertLess(present.index('before_present()'), present.index('comparison_notice.draw('))
        self.assertLess(present.index('comparison_notice.draw('), present.index('const HRESULT hr=fn('))
        self.assertLess(present.index('const HRESULT hr=fn('), present.index('comparison_begin_frame(ctx)'))
        self.assertLess(present.index('struct NoticePin'), present.index('HookGuard lock'))
        self.assertIn('std::shared_ptr<Device> owner;', present)
        self.assertIn('notice_pin.owner=owner', present)
        self.assertLess(present.index('BloomOperation internal(ctx)'), present.index('comparison_notice.draw('))
        self.assertIn('comparison_state_failed(notice.restore)', present)
        self.assertEqual(present.count('const HRESULT hr=fn(d,a,b,w,r)'), 1)
        self.assertIn('const bool down=(GetAsyncKeyState(VK_F8)&0x8000)!=0;', present)
        self.assertIn('ctx.comparison_notice.visible(GetTickCount64()) && comparison_foreground()', present)
        motion_source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        polling = extract_function(capture, 'void comparison_begin_frame(')
        # Ordinary launches (no HDR AgX, no ambient occlusion) return before any key or foreground query.
        self.assertLess(polling.index('if(!hdr_compare && !ambient_occlusion_requested && !emitter_compare && !sun_shadow_apply_requested && !fps_overlay_requested)return;'),
                        polling.index('comparison_foreground()'))
        self.assertIn('const bool hdr_compare=hdr_requested && hdr_config.tonemap==renderer::HdrTonemap::Agx;', polling)
        self.assertLess(polling.index('if(action.ambient_occlusion)ctx.motion_output.ambient_occlusion_toggle();'),
                        polling.index('if(!action.exposure && !action.bloom)return;'))
        # Emitter A/B keys: F4 the hull-program emitters (its own flag, so the
        # population is judged apart from the effects gain), F5 additive, F6
        # the emission source gain of all twenty pairs (F7 is the telemetry
        # marker, F8 the capture key; the 2026-09-16 effect-family key and its
        # option stay removed). The
        # keys are polled only inside the comparison sampler, so an ordinary
        # launch stays at zero queries; inside it they are unconditional, so
        # an option that was not requested answers with a logged refusal.
        self.assertIn('const bool emitter_compare=screen_emission_additive_requested'
                      ' || emission_source_gain!=1.f || hull_emission_gain!=1.f || hull_lightmap_gain!=1.f;', polling)
        for key, call in (('VK_F4', 'ctx.motion_output.hull_emission_gain_toggle()'),
                          ('VK_F5', 'ctx.motion_output.screen_emission_additive_toggle()'),
                          ('VK_F6', 'ctx.motion_output.emission_source_gain_toggle()')):
            self.assertIn(f'(GetAsyncKeyState({key})&0x8000)!=0;', polling)
            self.assertEqual(capture.count(f'GetAsyncKeyState({key})'), 1, 'one owner per function key')
            self.assertIn(call, polling)
        for key, label in (('ctrl_shift_f4', 'HULL'), ('ctrl_shift_f5', 'BULLETS'), ('ctrl_shift_f6', 'EMISSION')):
            self.assertIn(f'comparison_emitter(ctx,"{key}","{label}"', polling)
        for removed in ('effect_source_gain', 'X3M_EFFECT_SOURCE_GAIN'):
            self.assertNotIn(removed, capture)
        # F7 rule: telemetry.cpp owns Ctrl+Shift+F7 (its marker requires Shift);
        # capture.cpp's one F7 poller is the FPS overlay's Ctrl+Alt+F7 with
        # Shift up, so the chords are disjoint (comparison-hotkeys.md, "FPS overlay").
        self.assertEqual(capture.count('GetAsyncKeyState(VK_F7)'), 1)
        controls = (ROOT / 'src/proxy/comparison_controls.h').read_text()
        self.assertIn('keys.alt=fps_overlay_requested && (GetAsyncKeyState(VK_MENU)&0x8000)!=0;', polling)
        self.assertIn('keys.fps_overlay=fps_overlay_requested && (GetAsyncKeyState(VK_F7)&0x8000)!=0;', polling)
        self.assertIn('result.fps_overlay = keys.control && keys.alt && !keys.shift && keys.fps_overlay && !fps_overlay_down_;', controls)
        telemetry_source = (ROOT / 'src/proxy/telemetry.cpp').read_text()
        self.assertEqual(telemetry_source.count('GetAsyncKeyState(VK_F7)'), 1)
        self.assertIn('(GetAsyncKeyState(VK_F7)&0x8000)!=0 && (GetAsyncKeyState(VK_CONTROL)&0x8000)!=0 && (GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;', telemetry_source)
        emitter = extract_function(capture, 'void comparison_emitter(')
        self.assertIn('state<0?"UNAVAILABLE":state?"ON":"OFF"', emitter)
        self.assertIn('comparison_log(ctx,"request",key,state>=0);', emitter)
        notice = extract_function(capture, 'void comparison_notice_text(')
        self.assertIn('if(ctx.comparison_emitter_notice[0])', notice)
        # A refused toggle changes no state and creates nothing; the enabled
        # flag only gates the per-draw selection of the prebuilt variant.
        for signature, flag in (('int MotionOutput::screen_emission_additive_toggle(',
                                 'screen_additive_enabled_ = !screen_additive_enabled_;'),
                                ('int MotionOutput::emission_source_gain_toggle(',
                                 'source_gain_enabled_ = !source_gain_enabled_;'),
                                ('int MotionOutput::hull_emission_gain_toggle(',
                                 'hull_gain_enabled_ = !hull_gain_enabled_;')):
            body = extract_function(motion_source, signature)
            self.assertIn(f'if (available) {flag}', body)
            self.assertIn('return available ?', body)
            self.assertNotIn('CreatePixelShader', body)
        # Gain 1 creates no source-gain variant, so F6 refuses it; the
        # additive option at gain 1 still changes the draw (DESTBLEND ONE
        # and any alpha attenuation), so F5 stays available.
        self.assertIn('const bool available = screen_additive_requested_;',
                      extract_function(motion_source, 'int MotionOutput::screen_emission_additive_toggle('))
        self.assertIn('emission_source_gain_requested_ && emission_source_gain_ != 1.f',
                      extract_function(motion_source, 'int MotionOutput::emission_source_gain_toggle('))
        # Several emitter keys in one sample each keep their notice label.
        self.assertIn("if(emitter)ctx.comparison_emitter_notice[0]='\\0';", polling)
        self.assertIn('const std::size_t used=std::strlen(ctx.comparison_emitter_notice);', emitter)
        draws = extract_function(motion_source, 'MotionRoute MotionOutput::before_draw(')
        self.assertIn('shadow_.screen_additive_pair && screen_additive_enabled_ &&', draws)
        self.assertIn('shadow_.source_gain_eligible_variant && source_gain_enabled_', draws)
        # The hull emitters read their own flag: F6 no longer reaches them.
        self.assertIn('shadow_.ps_hull_program && hull_gain_enabled_ && route.submit', draws)
        self.assertNotIn('hull', extract_function(motion_source, 'int MotionOutput::emission_source_gain_toggle('))
        # One hull-emission flag: the ONE/ONE emitters and the hull light-map gain
        # (--hull-lightmap-gain) share F4; the toggle logs both states.
        hull_toggle = extract_function(motion_source, 'int MotionOutput::hull_emission_gain_toggle(')
        self.assertIn('const bool available = hull_emission_gain_requested_ || hull_lightmap_gain_requested_;', hull_toggle)
        self.assertIn('lightmap_requested=%u lightmap_gain=%g', hull_toggle)
        self.assertIn('shadow_.hull_lightmap_pair && hull_gain_enabled_ &&', extract_function(motion_source, 'HRESULT MotionOutput::bind_variant_pair('))
        # One additive telemetry line per Present, counters reset every frame.
        additive = extract_function(motion_source, 'void MotionOutput::log_screen_additive_frame(')
        self.assertIn('screen_emission_additive_frame device=%llu frame=%llu admitted=%u refused=%u pairs=%03x toggled=%u', additive)
        present = extract_function(motion_source, 'void MotionOutput::after_present(')
        self.assertIn('if (telemetry_) log_screen_additive_frame();', present)
        self.assertIn('screen_additive_frame_admitted_ = screen_additive_frame_refused_ = screen_additive_frame_pairs_ = 0;', present)
        reset = extract_function(capture, 'HRESULT reset_common(')
        self.assertIn('ctx.comparison_notice.hide();ctx.comparison.reset_focus()', reset)
        handoff = extract_function(capture, 'void retain_compositor_scene(')
        self.assertIn('call.input.filter.strength=ctx.comparison.bloom_requested?1.f:0.f;', handoff)
        hdr = (ROOT / 'src/renderer/hdr_pass.cpp').read_text()
        self.assertIn('caps_.tonemap && config_.meter_requested()', hdr)
        defaults = capture.split('hdr_config=x3m::renderer::HdrConfig{};', 1)[1].split('hdr_config.allow_auto_toggle=true;', 1)[0]
        self.assertIn('hdr_config.exposure=x3m::renderer::ExposureMode::Auto;', defaults)
        self.assertIn('hdr_config.params.ev_max=1.3f;', defaults)
        self.assertIn('if(GetEnvironmentVariableW(L"X3M_HDR_EV_MAX",setting,32)>0)', capture)
        self.assertIn('hdr_config.allow_auto_toggle=true;', capture)
        self.assertIn('if (caps_.meter) ensure_chain(width, height)', hdr)
        toggle = extract_function(motion_source, 'bool MotionOutput::comparison_toggle_exposure(')
        self.assertIn('invalidate_taa(TaaInvalidateSite::ComparisonExposure)', toggle)
        failure = extract_function(motion_source, 'void MotionOutput::comparison_state_failed(')
        self.assertIn('FAILED(result) && !motion_state_lost_', failure)
        self.assertIn('motion_state_lost_ = true; motion_state_error_ = result', failure)


if __name__ == '__main__':
    unittest.main()
