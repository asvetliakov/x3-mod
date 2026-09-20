"""Production card admission, warm-up/fault policy and mask rollback; no Wine."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include "fog_card_policy.h"
#include "fog_card_mask.h"
#include "fog_card_match.h"
#include <cassert>
#include <cstdio>
using namespace x3m;
int main() {
    assert(fog_card_pair(0x7b6393fe2d3e1d85ull, 0xf7e0b6647a3bfa62ull));
    assert(!fog_card_pair(0, 0xf7e0b6647a3bfa62ull));
    assert(!fog_card_pair(0x7b6393fe2d3e1d85ull, 0));
    FogCardShape shape{true,false,true,true,true,true,4,2,4,24,0,16,1,0x0cdf6a8c884ad955ull};
    assert(shape.matches());
    for (auto member : {&FogCardShape::topology, &FogCardShape::primitives, &FogCardShape::vertices,
                       &FogCardShape::stride, &FogCardShape::position_offset, &FogCardShape::position_type, &FogCardShape::frequency}) {
        auto changed = shape; ++(changed.*member); assert(!changed.matches());
    }
    for (auto member : {&FogCardShape::indexed, &FogCardShape::user_memory, &FogCardShape::stream0,
                       &FogCardShape::indices, &FogCardShape::stream0_only, &FogCardShape::frequency_known}) {
        auto changed = shape; changed.*member = !(changed.*member); assert(!changed.matches());
    }
    auto unknown = shape; unknown.declaration = 0; assert(!unknown.matches());
    FogCardStates states{0,0,0,1,7,1,0,3,2,4,1,0}; assert(states.matches());
    for (auto member : {&FogCardStates::z, &FogCardStates::zwrite, &FogCardStates::alpha_test, &FogCardStates::blend,
                       &FogCardStates::color_mask, &FogCardStates::cull, &FogCardStates::stencil, &FogCardStates::fill,
                       &FogCardStates::source, &FogCardStates::destination, &FogCardStates::operation, &FogCardStates::separate_alpha}) {
        auto changed = states; changed.*member = -1; assert(!changed.matches());
        changed = states; ++(changed.*member); assert(!changed.matches());
    }
    FogCardPolicy p;
    p.begin(true); assert(p.warmup && !p.may_replace() && p.medium_allowed());
    p.finish(false); p.begin(true); assert(!p.may_replace()); // failed warm-up stays vanilla
    p.finish(true); p.begin(true); assert(p.may_replace());
    p.reject(); assert(!p.may_replace() && !p.medium_allowed()); p.finish(false);
    p.begin(true); assert(p.may_replace()); // a refusal before suppression is recoverable
    ++p.suppressed; p.finish(false); assert(p.fault && !p.medium_allowed());
    p.begin(false); p.begin(true); assert(p.fault && !p.may_replace() && !p.medium_allowed());
    p = {}; p.begin(true); assert(p.warmup); p.finish(true); p.begin(true); ++p.suppressed;
    p.begin(true); assert(p.fault); // scene end never arrived
    p = {}; p.begin(true); p.finish(true); p.begin(false); p.begin(true); assert(p.warmup); // F9 rewarm
    // Setter failure both with and without mutation must restore exact mask.
    for (bool mutate : {false,true}) for (bool restore_fails : {false,true}) {
        FogCardMask mask; unsigned calls = 0, value = 7;
        mask.begin(7, [&](unsigned v) -> int { ++calls; if (calls == 1) { if (mutate) value = v; return -1; }
            if (restore_fails) return -2; value = v; return 0; });
        assert(calls == 2 && !mask.masked && mask.operation == -1 && mask.restore == (restore_fails ? -2 : 0));
        if (!restore_fails) assert(value == 7);
    }
    for (int draw_result : {0,-7}) {
        FogCardMask mask; unsigned calls = 0, value = 7, draws = 0;
        auto set = [&](unsigned v) { ++calls; value = v; return 0; };
        mask.begin(7, set); assert(value == 0 && mask.masked); ++draws; const int result = draw_result;
        mask.end(set); assert(value == 7 && !mask.masked && mask.restore == 0 && draws == 1 && calls == 2 && result == draw_result);
    }
    FogCardMask bad_restore; bad_restore.begin(7, [](unsigned) { return 0; });
    bad_restore.end([](unsigned) { return -9; }); assert(bad_restore.restore == -9);
    std::puts("fog cards policy/match/mask PASS");
}
'''


class FogCardPolicyTests(unittest.TestCase):
    def test_production_policy_match_and_mask(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'cards.cpp'
            binary = Path(tmp) / 'cards'
            source.write_text('#include <initializer_list>\n' + DRIVER)
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/proxy'), str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('PASS', result.stdout)

    def test_actual_motion_output_methods(self):
        fragment = (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
        names = ('fog_transition_invalidate', 'volumetric_fog_sector_sample', 'fog_card_transition', 'fault_fog_cards', 'prepare_volumetric_fog_targets', 'reconcile_volumetric_fog', 'complete_volumetric_fog', 'volumetric_fog_begin_frame',
                 'prepare_fog_card', 'finish_fog_card', 'volumetric_fog_toggle', 'volumetric_fog_step',
                 'run_volumetric_fog', 'disable_volumetric_fog')
        methods = []
        cpp = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        names += ('get_render_state_native', 'state_known', 'blend_known', 'state_field', 'begin_draw_reads')
        for name in names:
            fragment = cpp if name in ('get_render_state_native', 'state_known', 'blend_known', 'state_field', 'begin_draw_reads') else (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
            start = fragment.rfind('\n', 0, fragment.index('MotionOutput::' + name + '(')) + 1
            body = fragment.index('{', start)
            depth, end = 1, body + 1
            while depth:
                depth += (fragment[end] == '{') - (fragment[end] == '}')
                end += 1
            methods.append(fragment[start:end])
        # Execute the exact fog policy reset from before_reset; GPU release and
        # native Reset remain covered by the existing real-D3D fixtures.
        reset_start = cpp.index('    fog_sector_ = {}; fog_cards_ = {};')
        reset_end = cpp.index('\n', cpp.index('fog_failures_ = 0; fog_attach_failed_ = false;', reset_start))
        methods.append('void MotionOutput::reset_fog_for_test() noexcept {\n' + cpp[reset_start:reset_end] + '\n}')
        driver = '#include "fog_card_motion_mock.h"\nnamespace x3m {\n' + '\n'.join(methods) + '\n}\n#include "fog_card_motion_cases_inc.h"\n'
        compiler = shutil.which('clang++') or shutil.which('g++')
        with tempfile.TemporaryDirectory() as tmp:
            source, binary = Path(tmp) / 'motion.cpp', Path(tmp) / 'motion'
            source.write_text(driver)
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'src/proxy'), '-I', str(ROOT / 'src/renderer'),
                            '-I', str(ROOT / 'verification/probe'), str(source), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('actual MotionOutput card methods PASS', result.stdout)
            self.assertIn('cut_sequence frames=32 cuts=11 warmup=0 suppressed=192 applied=32 PASS', result.stdout)
            self.assertIn('cut_recovery scenarios=8 PASS', result.stdout)
            for cards in (6, 8):
                self.assertIn(f'card_native_calls cards={cards} rs_get={12*cards} freq_get={cards} mask_set={2*cards} total={15*cards}', result.stdout)

    def test_draw_integration_envelope_and_order(self):
        cpp = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        fog = (ROOT / 'src/proxy/motion_output_fog_inc.h').read_text()
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        draw = cpp[cpp.index('MotionRoute MotionOutput::before_draw('):cpp.index('// Canonical COM identity')]
        self.assertLess(draw.index('restore_bindings_checked()'), draw.index('prepare_fog_card(call, route)'))
        after = cpp[cpp.index('void MotionOutput::after_draw('):]
        self.assertLess(after.index('finish_fog_card(route, result)'), after.index('if (!enabled_'))
        bracket = fog[fog.index('void MotionOutput::prepare_fog_card('):fog.index('int MotionOutput::volumetric_fog_toggle')]
        self.assertNotIn('GetRenderState', bracket)
        self.assertIn('direct_call<GetStreamFreqFn>(GetStreamSourceFreq, 0, &shape.frequency)', bracket)
        self.assertLess(bracket.index('!shape.static_matches()'), bracket.index('direct_call<GetStreamFreqFn>'))
        self.assertLess(draw.index('if (!state_hooks_) begin_draw_reads()'), draw.index('prepare_fog_card(call, route)'))
        self.assertNotIn('volumetric_fog_cards_replace?"fog_cards"', capture)
        self.assertNotIn('hooked.set(102', capture)
        self.assertNotIn('taa_call(', bracket)
        self.assertIn('call_preserved([&]', bracket)
        self.assertIn('motion_state_lost_ = true', bracket)
        self.assertIn('route.submit = false', bracket)
        self.assertLess(bracket.index('fog_latch_.card(frame_)'), bracket.index('may_replace()'))
        self.assertLess(capture.index('if(action.fog_step)'), capture.index('ctx.motion_output.volumetric_fog_begin_frame()'))
        self.assertNotIn('set_stream_frequency', capture)


if __name__ == '__main__':
    unittest.main()
