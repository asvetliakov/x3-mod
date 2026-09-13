"""Execute production WRAP state transaction with a scripted documented-API device.

No Wine or shader bytes. Separately checks all available archive profile rows
against locally extracted original declaration semantics when that data exists.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class MotionWrapStatesTests(unittest.TestCase):
    def test_production_transaction(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        functions = [
            'void MotionOutput::set_render_state(',
            'HRESULT MotionOutput::get_render_state_native(',
            'HRESULT MotionOutput::render_state(',
            'void MotionOutput::invalidate_render_states(',
            'HRESULT MotionOutput::apply_wrap_states(',
            'HRESULT MotionOutput::restore_wrap_states(',
            'void MotionOutput::recover_motion_state(',
            'HRESULT MotionOutput::undo(',
            'HRESULT MotionOutput::bind_variant_pair(',
            'void MotionOutput::after_reset(',
            'void MotionOutput::begin_stateblock(',
            'void MotionOutput::end_stateblock(',
            'void MotionOutput::stateblock_applied(',
        ]
        states_start = source.index('constexpr D3DRENDERSTATETYPE shadow_states[')
        states_end = source.index('const char* scene_end_source_name', states_start)
        with tempfile.TemporaryDirectory(prefix='x3-motion-wrap-host-') as directory:
            output = Path(directory)
            (output / 'motion_wrap_under_test_inc.h').write_text(
                source[states_start:states_end] + '\n' +
                '\n\n'.join(extract_function(source, f) for f in functions))
            executable = output / 'fixture'
            compiled = subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2',
                '-Wall', '-Wextra', '-Werror', '-I', directory,
                str(ROOT / 'verification/probe/motion_wrap_state_fixture.cpp'), '-o', str(executable)],
                capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('failures=0', run.stdout)
            print(run.stdout.strip())

    def test_route_wiring(self):
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        evaluate = extract_function(source, 'void MotionOutput::evaluate_draw(')
        self.assertLess(evaluate.index('bind_targets(route)'), evaluate.index('apply_wrap_states(route,'))
        self.assertIn('rollback_route(route);', evaluate)
        rollback = extract_function(source, 'void MotionOutput::rollback_route(')
        self.assertIn('route.submit = false; route.submission_error = motion_state_error_;', rollback)
        before = extract_function(source, 'MotionRoute MotionOutput::before_draw(')
        self.assertLess(before.index('if (motion_state_lost_)'), before.index('evaluate_draw(call, route)'))
        self.assertIn('else if (route.submit) prepare_emission(call, route)', before)
        after = extract_function(source, 'void MotionOutput::after_draw(')
        self.assertIn('undo(route);', after)
        apply_wrap = extract_function(source, 'HRESULT MotionOutput::apply_wrap_states(')
        self.assertIn('shadow_.material_contract', apply_wrap)
        self.assertNotIn('linear_material_pair_contract(', apply_wrap)
        bind = extract_function(source, 'HRESULT MotionOutput::bind_variant_pair(')
        self.assertIn('return bind_variant_pair(route, false);', bind)
        self.assertIn('route.linear_material = material && SUCCEEDED(hr);', bind)
        resync = extract_function(source, 'void MotionOutput::resync_shadow(')
        self.assertIn('shadow_ = Shadow{};', resync)
        reset = extract_function(source, 'void MotionOutput::before_reset(')
        self.assertNotIn('motion_state_lost_ = false', reset)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertEqual(capture.count('.draw_submission_blocked()'), 4)
        self.assertEqual(capture.count('const HRESULT result=route.submit?'), 4)

    def test_original_semantic_absence(self):
        programs = Path(os.environ.get('X3M_SHADER_PROGRAMS', '/tmp/x3-shader-sweep/programs'))
        if not programs.is_dir():
            self.skipTest('local original shader tokens not available')
        from check_motion_wrap_profiles import check
        result = check(programs)
        self.assertGreater(result['pairs'], 0)
        self.assertEqual(result['declaration_collisions'], 0)

    def test_shadow_index_contract(self):
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        self.assertIn('motion_shadow_state_count = 24;', header)
        self.assertIn('emission_state_lost_ || motion_state_lost_', header)
        self.assertIn('DWORD saved_wrap[6]{};', header)
        self.assertIn('renderer::LinearMaterialPairContract material_contract{};', header)
        self.assertNotIn('material_sampler_mask', header)


if __name__ == '__main__':
    unittest.main()
