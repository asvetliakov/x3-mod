"""Host tests of the default-off frame-time diagnostic: the window reduction in
src/proxy/frame_timing.h compiled and run on the host, and the --frame-timing
launcher option. No game, no Wine, no device."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class FrameTimingWindow(unittest.TestCase):
    def test_window_statistics_buckets_and_live_accumulation(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-frame-timing-') as temporary:
            executable = Path(temporary) / 'frame_timing_host'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                # The probe compiles src/proxy/frame_timing.cpp itself against
                # the Win32 stand-in, so the accumulation is executed here.
                '-I', str(ROOT / 'verification/probe/frame_timing_standin'),
                str(ROOT / 'verification/probe/frame_timing_host.cpp'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'frame_timing_host checks=452 failures=0\n')
            self.assertEqual(run.stderr, '')

    def test_production_call_sites_and_schema(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        # Per-frame sample outside the 300-frame frame_end cadence, timing
        # around the forwarded Present only, primitives from the draw path.
        self.assertIn('frame_timing::frame(ctx.frame,ctx.draws);', capture)
        # present_begin ahead of cpu.before_original (pre-call instrumentation
        # must not alter the native input state), present_end after after_original.
        present = capture.split('const HRESULT hr=fn(d,a,b,w,r);')
        self.assertEqual(len(present), 2)
        self.assertLess(present[0].rindex('frame_timing::present_begin();'), present[0].rindex('cpu.before_original();'))
        self.assertTrue(present[1].startswith('cpu.after_original();\n    frame_timing::present_end();'), present[1][:120])
        self.assertIn('frame_timing::draw(primitives);', capture)
        # Bucket scopes: the two shared dispatch guards carry the scope of the
        # hooked entry point, with the lock member declared first so the stamps
        # are taken under the hook mutex; the draw hooks and the scene-end path
        # override the default State bucket.
        for guard, held in (('struct HookGuard {', 'HeldHookLock held;'), ('struct PlainHookGuard {', 'std::lock_guard<std::recursive_mutex> lock{mutex};')):
            body = capture[capture.index(guard):]
            body = body[:body.index('\n};')]
            self.assertLess(body.index(held), body.index('frame_timing::Scope timing;'), guard)
            self.assertIn('frame_timing::Bucket bucket=frame_timing::Bucket::State', body)
            self.assertIn('const char* entry=__builtin_FUNCTION()', body)
        self.assertEqual(capture.count('HookGuard lock(frame_timing::Bucket::Draw);'), 4)
        self.assertEqual(capture.count('HookGuard lock(frame_timing::Bucket::Scene);'), 2)
        for entry in ('scene_end_signal', 'compositor_pre', 'compositor_post'):
            self.assertIn(f'frame_timing::Scope timing(frame_timing::Bucket::Scene,"{entry}");', capture)
        # The native-draw stamps keep the Present pattern: begin ahead of
        # before_original, end after after_original, outside the CPU envelope.
        self.assertEqual(capture.count('frame_timing::draw_native_begin();'), 4)
        self.assertEqual(capture.count('frame_timing::draw_native_end();'), 4)
        for piece in capture.split('frame_timing::draw_native_begin();')[1:]:
            head = piece[:piece.index('frame_timing::draw_native_end();')]
            self.assertLess(head.index('cpu.before_original();'), head.index('cpu.after_original();'))
            self.assertNotIn('cpu.after_original();', piece[piece.index('frame_timing::draw_native_end();'):][:40])
        self.assertIn('frame_timing::initialize();', capture)
        self.assertEqual(capture.count('log("frame_end device=%llu frame=%llu draws=%llu'), 1)
        source = (ROOT / 'src/proxy/frame_timing.cpp').read_text()
        self.assertIn('frame_timing frame=%llu frames=%u dt_p50_us=%llu dt_p95_us=%llu dt_max_us=%llu '
                      'draws_p50=%llu draws_max=%llu present_p50_us=%llu present_p95_us=%llu '
                      'present_max_us=%llu', source)
        self.assertIn('" draw_p50_us=%llu draw_p95_us=%llu draw_max_us=%llu draw_native_p50_us=%llu draw_native_max_us=%llu"', source)
        # state_us is signed: -1 when no state call is stamped and no
        # calibration constant exists.
        self.assertIn('" scene_p50_us=%llu scene_p95_us=%llu scene_max_us=%llu state_p50_us=%lld state_p95_us=%lld state_max_us=%lld"', source)
        self.assertIn('" draw_calls_p50=%llu scene_calls_p50=%llu state_calls_p50=%llu state_sampled=%u slow=%u"', source)
        # The three gaps of the unhooked time, and the per-draw scene-traversal
        # cost of the window median.
        self.assertIn('" gap_pre_p50_us=%llu gap_pre_p95_us=%llu gap_pre_max_us=%llu"', source)
        self.assertIn('" gap_draw_p50_us=%llu gap_draw_p95_us=%llu gap_draw_max_us=%llu"', source)
        self.assertIn('" gap_post_p50_us=%llu gap_post_p95_us=%llu gap_post_max_us=%llu gap_draw_per_draw_us=%llu.%03llu"', source)
        self.assertIn('" state_top=%s state_other_p50=%llu"', source)
        # The three count-only window diagnostics (run 31): the redundancy
        # fields on the window line, and one line each for the program-pair
        # mix and the batchability classes.
        self.assertIn('" state_redundant=%llu,%llu,%llu state_shadowed=%llu,%llu,%llu redundant_top=%s"', source)
        self.assertIn('draw_pairs frame=%llu draws=%llu draw_pairs_overflow=%llu top=%s cutout_pairs=%llu,%llu', source)
        self.assertIn('draw_batch frame=%llu same_mesh=%llu same_mesh_any_range=%llu same_material=%llu draws=%llu', source)
        # The cutout pairs come from the single source, not from a literal here.
        self.assertIn('cutout::pair_hashes[2], cutout::pair_hashes[3]', source)
        self.assertIn('cutout::pair_hashes[0], cutout::pair_hashes[1]', source)
        # Every window counter restarts with the window.
        self.assertIn('draw_pairs.reset(); redundant_states.reset(); draw_batch.reset();', source)
        self.assertIn('draw_batch.end_frame(); // draws are only compared inside one frame', source)
        # One capture.cpp helper on the draw path, gated on the option, reading
        # the binding shadow the proxy already keeps (no Get* call per draw).
        helper = capture[capture.index('void frame_timing_draw_state('):]
        helper = helper[:helper.index('\nvoid snapshot(')]
        self.assertIn('if (!frame_timing::active) return;', helper)
        self.assertIn('ctx.motion_output.binding_shadow();', helper)
        self.assertNotIn('->Get', helper)
        self.assertEqual(capture.count('frame_timing_draw_state(ctx,type,primitives,base_vertex,start_index);'), 1)
        # The redundancy counting sits in the shadow update, not in the hook
        # bodies, and elides nothing.
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        for setter, call in (
                ('void MotionOutput::set_render_state(',
                 'frame_timing::state_write(frame_timing::StateSet::RenderState, unsigned(state),'),
                ('void MotionOutput::set_sampler_state(',
                 'frame_timing::state_write(frame_timing::StateSet::SamplerState, unsigned(type),'),
                ('void MotionOutput::set_texture(',
                 'frame_timing::state_write(frame_timing::StateSet::Texture, unsigned(stage), true,')):
            body = extract_function(motion, setter)
            self.assertIn(call, body, setter)
            self.assertNotIn('return;  // redundant', body)
        self.assertEqual(capture.count('frame_timing::state_write('), 0)
        self.assertIn('frame_timing_slow frame=%llu dt_us=%llu draws=%llu present_us=%llu prims=%llu', source)
        self.assertIn('" draw_us=%llu draw_native_us=%llu scene_us=%llu state_us=%lld"', source)
        self.assertIn('" draw_calls=%llu scene_calls=%llu state_calls=%llu slow_call=%s slow_call_us=%llu"', source)
        self.assertIn('" gap_pre_us=%llu gap_draw_us=%llu gap_post_us=%llu"', source)
        self.assertIn('X3M_FRAME_TIMING_STATE_STAMPS', source)
        self.assertIn('X3M_FRAME_TIMING', source)
        # The unstamped state path: one per-entry increment, no clock read.
        begin = source[source.index('void scope_begin_impl'):]
        begin = begin[:begin.index('\n}')]
        state_branch = begin[begin.index('if (bucket == state_bucket) {'):begin.index('const DWORD saved')]
        self.assertNotIn('stamp()', state_branch)
        self.assertIn('count_state_entry(entry);', state_branch)
        self.assertIn('if (!state_stamps || ++state_stamp_counter[slot] < state_stamps) return;', state_branch)
        # The sampled estimate, not the raw sampled ticks, enters the hooked
        # total, so the gaps and the buckets sum to dt at any stride.
        self.assertIn('hooked_ticks += state.bucket == state_bucket ? ticks * state_stamps : ticks;', source)

    def test_schema_documents_the_state_stamps_and_the_gaps(self):
        schema = (ROOT / 'docs/verification/sampling-profiler.md').read_text()
        section = schema[schema.index('## Frame timing diagnostic'):]
        section = section[:section.index('\n## ')]
        for field in ('state_sampled=', 'gap_pre_p50_us=', 'gap_draw_p50_us=', 'gap_post_p50_us=',
                      'gap_draw_per_draw_us=', 'state_top=', 'state_other_p50=',
                      'gap_pre_us=', 'gap_draw_us=', 'gap_post_us=',
                      'state_redundant=', 'state_shadowed=', 'redundant_top=',
                      'draw_pairs ', 'draw_pairs_overflow=', 'cutout_pairs=',
                      'draw_batch ', 'same_mesh=', 'same_mesh_any_range=', 'same_material='):
            self.assertIn(field, section, field)
        self.assertIn('X3M_FRAME_TIMING_STATE_STAMPS', section)
        self.assertIn('--frame-timing-state-stamps', section)
        self.assertIn('state_us=-1', section)
        # gap_draw is game time between hooked calls, not proxy time.
        self.assertIn('game time', section)
        # The per-draw evidence the next session B reads.
        shadows = (ROOT / 'docs/verification/directional-shadows.md').read_text()
        self.assertIn('draw_pairs', shadows)
        self.assertIn('cutout_pairs=', shadows)


class FrameTimingLaunchOption(unittest.TestCase):
    def test_launch_option_requires_telemetry_and_resets_inherited_value(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--frame-timing')
            self.assertEqual(code, 2)
            self.assertIn('--frame-timing requires --telemetry', error)
            code, output, error = helper.launch(directory, '--frame-timing', '--telemetry')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_FRAME_TIMING'], '1')
            code, output, error = helper.launch(directory, inherited={'X3M_FRAME_TIMING': '1'})
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_FRAME_TIMING'], '0')

    def test_state_stamps_option_requires_frame_timing_and_exports_the_interval(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--telemetry', '--frame-timing-state-stamps', '8')
            self.assertEqual(code, 2)
            self.assertIn('--frame-timing-state-stamps requires --frame-timing', error)
            code, output, error = helper.launch(
                directory, '--telemetry', '--frame-timing', '--frame-timing-state-stamps', '8')
            self.assertEqual(code, 0, error)
            environment = json.loads(output)['env']
            self.assertEqual(environment['X3M_FRAME_TIMING'], '1')
            self.assertEqual(environment['X3M_FRAME_TIMING_STATE_STAMPS'], '8')
            # Default: the calls are counted, never stamped.
            code, output, error = helper.launch(directory, '--telemetry', '--frame-timing')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_FRAME_TIMING_STATE_STAMPS'], '0')


if __name__ == '__main__':
    unittest.main()
