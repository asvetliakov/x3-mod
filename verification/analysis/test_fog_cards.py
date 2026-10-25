"""Production card admission, warm-up/fault policy and mask rollback; no Wine."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from source_text import source_text

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
        changed = states; changed.*member += 2; assert(!changed.matches()); // z 2 (USEW), cull 3 (CCW): refused
        changed = states; ++(changed.*member);
        assert(changed.matches() == (member == &FogCardStates::z || member == &FogCardStates::cull ||
                                     member == &FogCardStates::alpha_test || member == &FogCardStates::separate_alpha)); // the material's own z/cull; alpha test, separate alpha 0 or 1
    }
    FogCardStates docked{1,0,0,1,7,2,0,3,2,4,1,0}; assert(docked.matches()); // run278 docked-at-load variant
    docked.zwrite = 1; assert(!docked.matches());
    FogCardStates run283{0,0,1,1,7,1,0,3,2,4,1,0}; assert(run283.matches()); // measured docked-at-load vector
    for (long atest : {2L, -1L}) { auto changed = run283; changed.alpha_test = atest; assert(!changed.matches()); }
    for (auto member : {&FogCardStates::zwrite, &FogCardStates::stencil}) { auto changed = run283; changed.*member = 1; assert(!changed.matches()); }
    FogCardStates run143{0,0,0,1,7,1,0,3,2,4,1,1}; assert(run143.matches()); // measured native cockpit vector
    for (long sepalpha : {2L, -1L}) { auto changed = run143; changed.separate_alpha = sepalpha; assert(!changed.matches()); }
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
        fragment = source_text(ROOT / 'src/proxy/motion_output_fog_inc.h')
        names = ('fog_transition_invalidate', 'volumetric_fog_sector_sample', 'fog_prefill_confirm', 'fog_card_transition', 'fault_fog_cards', 'prepare_volumetric_fog_targets', 'reconcile_volumetric_fog', 'complete_volumetric_fog', 'volumetric_fog_begin_frame',
                 'prepare_fog_card', 'finish_fog_card', 'volumetric_fog_toggle', 'volumetric_fog_step',
                 'run_volumetric_fog', 'disable_volumetric_fog')
        methods = []
        cpp = source_text(ROOT / 'src/proxy/motion_output.cpp')
        names += ('get_render_state_native', 'state_known', 'blend_known', 'state_field', 'begin_draw_reads')
        for name in names:
            fragment = cpp if name in ('get_render_state_native', 'state_known', 'blend_known', 'state_field', 'begin_draw_reads') else source_text(ROOT / 'src/proxy/motion_output_fog_inc.h')
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
            # 2026-10-26: E_OUTOFMEMORY retried without a bound; rows for attempts 1-8, then every 10th; prepare_ok once
            self.assertIn('prepare_oom unbounded attempts=40 rows=12 ok_rows=1 PASS', result.stdout)
            self.assertIn('prepare_oom_video sector_reset=1 row_disabled_rows=0 PASS', result.stdout)  # OUTOFVIDEOMEMORY retried; another sector restarts the count
            self.assertIn('prefill_gap confirmed=1 discarded=1 later_gap=1 PASS', result.stdout)  # R3 gap hold and decision
            # case A: a same-key transit (another id) is a cold start; a heap-token change of the same sector keeps the field (Run75)
            self.assertIn('run273_transit same=1 token=1 id_same=1 realloc=1 id_change=1 run273=1 rekey=1 first=1 PASS', result.stdout)
            self.assertIn('run273_prefill_adopt adopted=1 discarded=1 PASS', result.stdout)  # case B: the confirmed key is adopted, the stale camera dropped
            self.assertIn('run273_card_refusal named=12 PASS', result.stdout)
            self.assertIn('plain_tier_cards counters_quiet=1 fault_line=1 PASS', result.stdout)  # 2026-10-21 tier split  # case C: the first refusal of a frame is named
            # run278/run283 case C: the material's own z/cull and the measured run283 vector (z 0, cull NONE, alpha test 1)
            # are admitted; zwrite/stencil 1, alpha test 2 and wrong blend/mask/fill refused; the state row spaced 300 frames
            # run143 (native cockpit): separate alpha 1 admitted, 2 and unknown refused
            self.assertIn('run278_docked_states admitted=10 refused=25 spaced=1 PASS', result.stdout)
            self.assertIn('volumetric_fog_card_states', source_text(ROOT / 'src/proxy/motion_output_fog_inc.h'))
            for cards in (6, 8):
                self.assertIn(f'card_native_calls cards={cards} rs_get={12*cards} freq_get={cards} mask_set={2*cards} total={15*cards}', result.stdout)

    def test_draw_integration_envelope_and_order(self):
        cpp = source_text(ROOT / 'src/proxy/motion_output.cpp')
        fog = source_text(ROOT / 'src/proxy/motion_output_fog_inc.h')
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
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
        # The frame boundary runs the fog begin step every frame (the Ctrl+Alt+F9/F10 keys that preceded it went on 2026-09-26).
        self.assertLess(capture.index('ctx.motion_output.begin_frame(ctx.frame,ctx.capture);'), capture.index('ctx.motion_output.volumetric_fog_begin_frame();'))
        self.assertNotIn('set_stream_frequency', capture)


class FogPassHorizonFaultTests(unittest.TestCase):
    """The production FogPass's horizon-level fallbacks (docs/architecture/fog-visible-distance.md §6) under fault
    injection: the MotionOutput harness above runs against a mock pass, so this one runs the real FogPass, DensityCache
    and generator against a scripted device (verification/probe/fog_pass_horizon_fault.cpp). A MinGW i686 build run
    natively through WSL interop; skipped on hosts without both (under Wine it runs through wine_lock.py by hand)."""
    SOURCES = ('verification/probe/fog_pass_horizon_fault.cpp', 'src/renderer/fog_pass.cpp', 'src/fog/fog_density_cache.cpp',
               'src/fog/fog_density_generator.cpp', 'src/renderer/gpu_sync_timing.cpp')

    def test_horizon_create_lock_upload_reset_and_program_faults(self):
        compiler = shutil.which('i686-w64-mingw32-g++')
        if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        with tempfile.TemporaryDirectory(prefix='x3-fog-fault-') as tmp:
            binary = Path(tmp) / 'fog_pass_horizon_fault.exe'
            subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse',
                            '-mstackrealign', '-mincoming-stack-boundary=2', '-ffp-contract=off', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX',
                            '-DX3M_FOG_PASS_FIXTURE', '-static', *[str(ROOT / s) for s in self.SOURCES], '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=600)
        self.assertEqual(result.returncode, 0, result.stdout[-3000:] + result.stderr[-1000:])
        self.assertIn('fog_pass_horizon_fault scenarios=9 failures=0 live_objects=0', result.stdout)
        rows = {line.split()[1]: line for line in result.stdout.splitlines() if line.startswith('SCENARIO ')}
        for name, reason in (('staging', 'staging'), ('atlas', 'atlas'), ('reset_atlas', 'atlas'), ('lock', 'lock'), ('upload', 'upload')):
            self.assertIn('result=00000000 available=1 cap=195000 horizon_refused=%s ' % reason, rows[name])
        self.assertIn('cap=337500 horizon_refused=- distance_refused=- horizon=1 ready=1.000,1.000,1.000 creates=6', rows['baseline'])
        self.assertIn('result=88760868', rows['lost'])  # D3DERR_DEVICELOST: a Reset, not a fallback
        self.assertIn('distance_refused=program horizon=0', rows['program'])
        self.assertIn('cap=0 horizon_refused=- distance_refused=- horizon=0', rows['distance_1'])


if __name__ == '__main__':
    unittest.main()
