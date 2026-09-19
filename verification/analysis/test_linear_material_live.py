"""Host execution of production live material control flow with scripted COM.

This checks registry/fallback/shadow lifetime, not shader math, Windows ABI or GPU
behavior. Functions are extracted unchanged; external APIs are explicit doubles.
"""
from pathlib import Path
import contextlib
import importlib.util
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class LinearMaterialLiveTests(unittest.TestCase):
    def test_composition_terminal_export_source_contract(self):
        # Policy wiring witness only: transaction and Reset behavior are exercised
        # by the extracted C++ seam; this does not claim GPU publication coverage.
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        end = extract_function(source, 'void MotionOutput::end_redirect(')
        assignments = [line.strip() for line in end.splitlines()
                       if 'composition_terminal_export_ =' in line]
        self.assertEqual(assignments, [
            'composition_terminal_export_ = reason == HdrEnd::Hook || reason == HdrEnd::BloomCopy || reason == HdrEnd::Present;',
            'composition_terminal_export_ = false;',
        ])
        end_scene = extract_function(source, 'void MotionOutput::before_end_scene() noexcept')
        self.assertEqual(' '.join(end_scene.split()),
                         'void MotionOutput::before_end_scene() noexcept { if (hdr_state_ == HdrState::Active) flush_redirect(); }')
        writeback = extract_function(source, 'renderer::HdrWriteback MotionOutput::hdr_writeback(')
        self.assertIn('if (write && composition_enhanced_ && !composition_terminal_export_ && !composition_diagnostic_export_) composition_export();', writeback)

    def test_render_state_hook_follows_the_hooked_configuration(self):
        # Hybrid unhook (state-call-fast-path.md, step 5): slot 57 is installed
        # exactly in the hooked configuration, which composition no longer
        # forces; the reasons are the explicit shadow, frame timing and a
        # failed Get* capability check (fail closed). Lazy RT mode is not one:
        # it never holds a write mask (route-per-draw-cost.md, lever 3).
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        install = next(line.strip() for line in capture.splitlines()
                       if 'hooked.set(57,set_render_state)' in line)
        self.assertEqual(install.replace(' ', ''), 'if(hooked.motion_output.state_hooks())hooked.set(57,set_render_state);')
        gate = capture[capture.index('const char* state_hooks_reason='):capture.index('hooked.motion_output.configure_state_hooks(state_hooks);')]
        self.assertNotIn('lazy_rt', gate)
        for reason in ('"explicit"', 'frame_timing::active', '"get_failed"', '(58)(d,D3DRS_ZENABLE', '(68)(d,0,D3DSAMP_SRGBTEXTURE'):
            self.assertIn(reason, gate)
        sampler = next(line.strip() for line in capture.splitlines() if 'hooked.set(69,set_sampler_state)' in line)
        self.assertTrue(sampler.startswith('if(hooked.motion_output.state_hooks()&&('), sampler)
        setter = extract_function(capture, 'HRESULT WINAPI set_render_state(')
        self.assertIn('if(SUCCEEDED(hr))ctx.motion_output.set_render_state(state,value);', setter)

    def test_unresolved_composition_capabilities_disable_reactive_baseline(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        policy = extract_function(source, 'if (composition_effective_ || composition_required_producers_)')
        self.assertLess(policy.index('in.reactive_policy = renderer::ReactivePolicy::Unavailable;'),
                        policy.index('composition_->coverage_valid()'))
        self.assertIn('!composition_frame_stopped_', policy)
        self.assertIn('in.reactive = composition_mask;', policy)

    def test_production_control_flow(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        signatures = [
            'constexpr unsigned composition_blend_index(D3DRENDERSTATETYPE state) noexcept',
            'constexpr unsigned shadow_index(D3DRENDERSTATETYPE state) noexcept',
            'void MotionOutput::set_render_state(D3DRENDERSTATETYPE state, DWORD value) noexcept',
            'unsigned MotionOutput::device_references() const noexcept',
            'void MotionOutput::release_resources() noexcept',
            'void MotionOutput::configure_linear_materials(bool requested, const renderer::LinearMaterialConfig& config) noexcept',
            'void MotionOutput::configure_linear_distance_fade(bool requested) noexcept',
            'void MotionOutput::configure_linear_emissions(bool requested, float gain) noexcept',
            'void MotionOutput::register_vertex_shader(',
            'void MotionOutput::register_pixel_shader(',
            'void MotionOutput::set_vertex_shader(',
            'void MotionOutput::set_pixel_shader(',
            'void MotionOutput::set_sampler_state(',
            'void MotionOutput::set_texture(',
            'int MotionOutput::composition_texture_reader(',
            'void MotionOutput::composition_export() noexcept',
            'void MotionOutput::release_composition_identity() noexcept',
            'void MotionOutput::before_texture_write(',
            'void MotionOutput::begin_composition_frame() noexcept',
            'void MotionOutput::prepare_composition(',
            'bool MotionOutput::publish_composition() noexcept',
            'void MotionOutput::finish_composition(',
            'MotionRoute MotionOutput::before_draw(',
            'void MotionOutput::restore_bindings() noexcept',
            'HRESULT MotionOutput::restore_bindings_checked() noexcept',
            'HRESULT MotionOutput::restore_mip_bias() noexcept',
            'void MotionOutput::restore_mip_bias_stage(',
            'template<bool quiet> HRESULT MotionOutput::flush_bindings() noexcept',
            'void MotionOutput::record_deferred() noexcept',
            'void MotionOutput::resync_samplers() noexcept',
            'void MotionOutput::refresh_linear_material_contract() noexcept',
            'void MotionOutput::report_xt_default_unavailable() noexcept',
            'void MotionOutput::refresh_linear_emission_contract() noexcept',
            'void MotionOutput::resync_shadow() noexcept',
            'void MotionOutput::begin_stateblock() noexcept',
            'void MotionOutput::end_stateblock() noexcept',
            'void MotionOutput::stateblock_applied() noexcept',
            'void MotionOutput::before_reset() noexcept',
            'void MotionOutput::after_reset(HRESULT result) noexcept',
            'void MotionOutput::recover_motion_state() noexcept',
            'unsigned MotionOutput::linear_material_refusal() noexcept',
            'HRESULT MotionOutput::bind_variant_pair(',
            'HRESULT MotionOutput::bind_targets(',
            'HRESULT MotionOutput::undo(',
            'void MotionOutput::rollback_route(',
        ]
        with tempfile.TemporaryDirectory(prefix='x3-linear-material-live-') as directory:
            path = Path(directory)
            capture = (ROOT / 'src/proxy/capture.cpp').read_text()
            start = capture.index('    const bool material_requested=GetEnvironmentVariableW')
            end = capture.index('    bloom_requested=GetEnvironmentVariableW', start)
            environment = 'void configure_environment() { wchar_t setting[32]{};\n' + capture[start:end] + '}\n'
            # Execute the exact completed-route counter block with cached
            # contracts, without duplicating the rest of evaluate_draw.
            count = extract_function(source, 'if (route.linear_material)')
            count = 'void MotionOutput::count_material_route(const MotionRoute& route) noexcept {\n' + count + '\n}\n'
            evaluate = extract_function(source, 'void MotionOutput::evaluate_draw(')
            begin = evaluate.index('    if (SUCCEEDED(hr)) {', evaluate.index('HRESULT hr = bind_variant_pair(route, material);'))
            end = evaluate.index('    if (SUCCEEDED(hr)) hr = bind_targets(route);', begin)
            constants = ('HRESULT MotionOutput::prepare_constants(MotionRoute& route) noexcept {\n'
                         'HRESULT hr=S_OK; bool matched=true; std::array<float,16> previous{};\n'
                         'const float zeros[16]{}, pixel[8]{}; previous.fill(99.f);\n' +
                         evaluate[begin:end] + 'return hr;\n}\n')
            # The compile-time shadow/blend index tables the setter reads
            # (docs/architecture/state-call-fast-path.md, dispatch trim),
            # with the production static_assert that they equal the scans.
            size = next(line for line in source.splitlines() if line.startswith('constexpr unsigned state_index_table_size'))
            match = next(line for line in source.splitlines() if line.startswith('static_assert(state_index_tables_match_scans()'))
            tables = '\n'.join([
                extract_function(source, 'constexpr unsigned composition_blend_index_scan('),
                extract_function(source, 'constexpr unsigned shadow_index_scan('),
                size,
                extract_function(source, 'struct StateIndexTables') + ';',
                extract_function(source, 'constexpr StateIndexTables make_state_index_tables() noexcept'),
                'constexpr StateIndexTables state_index_tables = make_state_index_tables();',
                extract_function(source, 'constexpr bool state_index_tables_match_scans() noexcept'),
                match]) + '\n'
            (path / 'linear_material_live_under_test_inc.h').write_text(
                tables + '\n\n'.join(extract_function(source, sig) for sig in signatures) + '\n' + environment + count + constants)
            executable = path / 'fixture'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', directory,
                                    str(ROOT / 'verification/probe/linear_material_live_fixture.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('failures=0', run.stdout)
            self.assertIn('linear_emission_cache checks=', run.stdout)
            self.assertIn('linear_material_xt_cache checks=', run.stdout)
            self.assertIn('linear_material_xt_deferred_notice checks=', run.stdout)
            self.assertIn('linear_emission_route checks=', run.stdout)
            self.assertIn('linear_distance_fade_cache checks=', run.stdout)
            self.assertIn('linear_distance_fade_route checks=', run.stdout)
            self.assertIn('linear_distance_fade_environment checks=', run.stdout)
            self.assertEqual(run.stderr, '')
            print(run.stdout.strip())

    def test_xt_notice_stays_out_of_lightweight_hooks(self):
        source=(ROOT/'src/proxy/motion_output.cpp').read_text()
        refresh=extract_function(source,'void MotionOutput::refresh_linear_material_contract() noexcept')
        self.assertNotIn('log(',refresh)
        self.assertNotIn('report_xt_default_unavailable(',refresh)
        for name in ('set_vertex_shader','set_pixel_shader'):
            setter=extract_function(source,'void MotionOutput::'+name+'(')
            self.assertIn('refresh_linear_material_contract();',setter)
            self.assertNotIn('log(',setter)
        present=extract_function(source,'void MotionOutput::after_present(')
        self.assertLess(present.index('report_xt_default_unavailable();'),present.index('if (!enabled_)'))
        release=extract_function(source,'void MotionOutput::release_resources() noexcept')
        self.assertIn('report_xt_default_unavailable();',release)
        report=extract_function(source,'void MotionOutput::report_xt_default_unavailable() noexcept')
        self.assertLess(report.index('event.pending = false'),report.index('log('))

    def test_xt_default_gate_precedes_jitter_and_native_submission(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        evaluate = extract_function(source, 'void MotionOutput::evaluate_draw(')
        gate = 'if (shadow_.xt_default_pair && !shadow_.xt_default_ready)'
        self.assertLess(evaluate.index(gate), evaluate.index('apply_jitter(route)'))
        self.assertLess(evaluate.index(gate), evaluate.index('bind_variant_pair(route, material)'))
        refusal = extract_function(evaluate, gate)
        self.assertIn('return;', refusal)
        self.assertNotIn('submit = false', refusal)
        # The existing hook invokes the native source only once and preserves
        # its result. XT availability does not add a draw call or API override.
        bind = extract_function(source, 'HRESULT MotionOutput::bind_variant_pair(')
        self.assertNotIn('Draw', bind)
        self.assertLess(bind.index('route.vs_set = true;'), bind.index('native<SetVsFn>'))
        self.assertLess(bind.index('route.ps_set = true;'), bind.index('native<SetPsFn>'))

    def launch(self, *args, environment=None):
        spec = importlib.util.spec_from_file_location('linear_material_manage', ROOT / 'tools/manage.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'X3AP.exe').touch()
            argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', directory, *args]
            stdout, stderr = io.StringIO(), io.StringIO()
            with patch('sys.argv', argv), patch.dict(os.environ, environment or {}), contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    module.main()
                    status = 0
                except SystemExit as error:
                    status = error.code
            return status, stdout.getvalue(), stderr.getvalue()

    def test_cli_dependencies_and_gain_bounds(self):
        valid = ('--motion-output', '--hdr', '--hdr-tonemap', '--linear-materials')
        rejected = [('--linear-materials',), ('--motion-output', '--hdr', '--linear-materials'),
                    (*valid, '--hdr-decode', 'none'), (*valid, '--hdr-decode', 'srgb')]
        for option in ('--material-direct-gain', '--material-emissive-gain', '--lightmap-emissive-gain'):
            rejected.append((option, '1'))
            for value in ('-1', '16.01', 'nan', 'inf', '-inf'):
                rejected.append((*valid, f'{option}={value}'))
        for args in rejected:
            with self.subTest(args=args):
                code, _, _ = self.launch(*args)
                self.assertEqual(code, 2)
        for value in ('0', '1', '4', '16'):
            code, output, error = self.launch(*valid, '--material-direct-gain', value, '--material-emissive-gain', value, '--lightmap-emissive-gain', value)
            self.assertEqual(code, 0, error)
            self.assertIn('X3M_LINEAR_MATERIALS', output)
            self.assertIn(f'"X3M_MATERIAL_DIRECT_GAIN": "{float(value)}"', output)
        code, _, error = self.launch(*valid, '--hdr-decode', 'pow22')
        self.assertEqual(code, 0, error)

    def test_material_fill_cli_dependency_bounds_and_default(self):
        valid = ('--motion-output', '--hdr', '--hdr-tonemap', '--linear-materials')
        status, _, message = self.launch('--material-fill', '0.06')
        self.assertEqual(status, 2)
        self.assertIn('--material-fill requires --linear-materials', message)
        for value in ('-0.01', '0.5001', 'nan', 'inf', '-inf'):
            with self.subTest(value=value):
                status, _, message = self.launch(*valid, f'--material-fill={value}')
                self.assertEqual(status, 2)
                self.assertIn('--material-fill must be finite and within [0, 0.5]', message)
        for value in ('0', '0.06', '0.5'):
            with self.subTest(value=value):
                status, output, error = self.launch(*valid, '--material-fill', value)
                self.assertEqual(status, 0, error)
                self.assertIn(f'"X3M_MATERIAL_FILL": "{float(value)}"', output)
        status, output, error = self.launch(*valid)
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_MATERIAL_FILL": "0.05"', output)
        status, output, error = self.launch(
            environment={'X3M_MATERIAL_FILL': '0.5', 'X3M_LINEAR_MATERIALS': '1'})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_MATERIAL_FILL": "0.0"', output)
        self.assertIn('"X3M_LINEAR_MATERIALS": "0"', output)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        defaults = capture.split('linear_material_config=x3m::renderer::LinearMaterialConfig{};', 1)[1].split('const auto material_gain', 1)[0]
        self.assertIn('linear_material_config.fill=0.05f;', defaults)
        self.assertIn('material_gain(L"X3M_MATERIAL_FILL",linear_material_config.fill,0.5f);', capture)

    def test_emission_cli_dependencies_and_gain_bounds(self):
        valid = ('--motion-output', '--taa', '--object-trace', '--object-lifetime', '--ownership', '--hdr', '--hdr-tonemap', '--linear-emissions')
        rejected = [tuple(item for item in valid if item != missing)
                    for missing in ('--motion-output', '--taa', '--hdr', '--hdr-tonemap')]
        rejected += [('--emission-gain', '1'), (*valid, '--hdr-decode', 'none'),
                     (*valid, '--hdr-decode', 'srgb')]
        rejected += [(*valid, f'--emission-gain={value}')
                     for value in ('-1', '16.01', 'nan', 'inf', '-inf')]
        for args in rejected:
            with self.subTest(args=args):
                status, _, _ = self.launch(*args)
                self.assertEqual(status, 2)
        for value in ('0', '1', '4', '16'):
            with self.subTest(gain=value):
                status, output, error = self.launch(*valid, '--emission-gain', value)
                self.assertEqual(status, 0, error)
                self.assertIn('"X3M_LINEAR_EMISSIONS": "1"', output)
                self.assertIn(f'"X3M_EMISSION_GAIN": "{float(value)}"', output)
                self.assertIn('"X3M_LINEAR_MATERIALS": "0"', output)
        status, output, error = self.launch(*valid, '--hdr-decode', 'pow22')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_EMISSION_GAIN": "1.0"', output)

    # The fade route is default on with its prerequisites since 2026-09-14
    # (runs 11/14/15); --no-linear-distance-fade opts out.
    PREREQUISITES = ('--motion-output', '--taa', '--object-trace', '--object-lifetime',
                     '--ownership', '--hdr', '--hdr-tonemap', '--linear-materials')

    def test_distance_fade_cli_dependencies_and_explicit_on(self):
        valid = (*self.PREREQUISITES, '--linear-distance-fade')
        for missing in ('--taa', '--linear-materials'):
            with self.subTest(missing=missing):
                status, _, _ = self.launch(*(arg for arg in valid if arg != missing))
                self.assertEqual(status, 2)
        status, output, error = self.launch(*valid)
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "1"', output)
        self.assertIn('"X3M_LINEAR_EMISSIONS": "0"', output)
        status, output, error = self.launch(environment={'X3M_LINEAR_DISTANCE_FADE': '1'})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "0"', output)

    def test_distance_fade_defaults_on_with_its_prerequisites(self):
        status, output, error = self.launch(*self.PREREQUISITES)
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "1"', output)
        # The default is enough for the witness dependency.
        status, output, error = self.launch(*self.PREREQUISITES, '--fade-witness')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_FADE_WITNESS": "30"', output)

    def test_distance_fade_default_stays_off_without_prerequisites(self):
        for missing in ('--taa', '--linear-materials'):
            with self.subTest(missing=missing):
                status, output, error = self.launch(*(arg for arg in self.PREREQUISITES if arg != missing))
                self.assertEqual(status, 0, error)
                self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "0"', output)
        status, output, error = self.launch()
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "0"', output)

    def test_no_distance_fade_wins_over_the_default_and_the_explicit_flag(self):
        status, output, error = self.launch(*self.PREREQUISITES, '--no-linear-distance-fade')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "0"', output)
        status, output, error = self.launch(*self.PREREQUISITES, '--linear-distance-fade', '--no-linear-distance-fade')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "0"', output)
        # Opting out also removes the witness dependency.
        status, _, _ = self.launch(*self.PREREQUISITES, '--no-linear-distance-fade', '--fade-witness')
        self.assertEqual(status, 2)
        # Without prerequisites the opt-out is accepted and stays off.
        status, output, error = self.launch('--no-linear-distance-fade')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_DISTANCE_FADE": "0"', output)

    def test_screen_emission_timing_requires_the_option_and_stays_off_by_default(self):
        # --screen-emission-timing is the option's opt-in per-frame diagnostic:
        # the option itself must not enable it, and it is refused alone.
        status, output, error = self.launch(*self.PREREQUISITES, '--screen-emission')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_SCREEN_EMISSION": "1"', output)
        self.assertIn('"X3M_SCREEN_EMISSION_TIMING": "0"', output)
        status, output, error = self.launch(*self.PREREQUISITES, '--screen-emission', '--screen-emission-timing')
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_SCREEN_EMISSION_TIMING": "1"', output)
        status, _, message = self.launch(*self.PREREQUISITES, '--screen-emission-timing')
        self.assertEqual(status, 2)
        self.assertIn('--screen-emission-timing', message)
        # A stale shell value cannot enable it.
        status, output, error = self.launch(environment={'X3M_SCREEN_EMISSION_TIMING': '1'})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_SCREEN_EMISSION_TIMING": "0"', output)

    def test_emission_cli_default_off_clears_inherited_values(self):
        status, output, error = self.launch(environment={'X3M_LINEAR_EMISSIONS': '1', 'X3M_EMISSION_GAIN': '16'})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_LINEAR_EMISSIONS": "0"', output)
        self.assertIn('"X3M_EMISSION_GAIN": "1.0"', output)

    def test_ambient_occlusion_cli_dependencies_and_defaults(self):
        base = ('--motion-output', '--taa', '--object-trace', '--object-lifetime', '--ownership')
        valid = (*base, '--ambient-occlusion')
        for missing in ('--taa', '--motion-output'):
            with self.subTest(missing=missing):
                status, _, _ = self.launch(*(arg for arg in valid if arg != missing))
                self.assertEqual(status, 2)
        for dependent in (('--ao-radius', '3'), ('--ao-strength', '0.3'), ('--ao-debug',), ('--ao-timing',)):
            with self.subTest(dependent=dependent):
                status, _, _ = self.launch(*base, *dependent)
                self.assertEqual(status, 2)
        for bad in (('--ao-radius', '0'), ('--ao-radius', '101'), ('--ao-strength', '1.5'), ('--ao-strength', 'nan')):
            with self.subTest(bad=bad):
                status, _, _ = self.launch(*valid, *bad)
                self.assertEqual(status, 2)
        status, output, error = self.launch(*valid, '--ao-timing')
        self.assertEqual(status, 0, error)
        for line in ('"X3M_AMBIENT_OCCLUSION": "1"', '"X3M_AO_RADIUS": "2.0"', '"X3M_AO_STRENGTH": "0.5"', '"X3M_AO_DEBUG": "0"', '"X3M_AO_TIMING": "1"'):
            self.assertIn(line, output)
        status, output, error = self.launch(*valid, '--ao-radius', '3.5', '--ao-strength', '0.25', '--ao-debug')
        self.assertEqual(status, 0, error)
        for line in ('"X3M_AO_RADIUS": "3.5"', '"X3M_AO_STRENGTH": "0.25"', '"X3M_AO_DEBUG": "1"'):
            self.assertIn(line, output)
        # Default off, and an inherited value cannot enable it.
        status, output, error = self.launch(*base, environment={'X3M_AMBIENT_OCCLUSION': '1', 'X3M_AO_DEBUG': '1'})
        self.assertEqual(status, 0, error)
        self.assertIn('"X3M_AMBIENT_OCCLUSION": "0"', output)
        self.assertIn('"X3M_AO_DEBUG": "0"', output)

    def test_cli_clears_inherited_feature_and_gains(self):
        code, output, error = self.launch(environment={'X3M_LINEAR_MATERIALS': '1', 'X3M_MATERIAL_DIRECT_GAIN': '16'})
        self.assertEqual(code, 0, error)
        self.assertIn('"X3M_LINEAR_MATERIALS": "0"', output)
        self.assertIn('"X3M_MATERIAL_DIRECT_GAIN": "1.0"', output)


if __name__ == '__main__':
    unittest.main()
