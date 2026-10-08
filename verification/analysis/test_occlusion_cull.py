"""Host checks of the occlusion cull (docs/architecture/occlusion-cull.md).

The pure core (src/proxy/occlusion_cull_core.h) compiled with the host compiler through
verification/probe/occlusion_cull_host.cpp: classification, test rectangle, stability guard, ring bookkeeping, skip rule
and the classifier over a synthetic engine image. Then the source contracts the fixture cannot see: the draw-site order
(after the small-prop cull, before the jitter), the pass's Reset and teardown under the reference accounting, the fixed
pool, the hand-encoded programs, the row fields, the schema entry without a budget setting, and the gate record. No
device, no Wine.
"""
import json
import re
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PASS_CPP = ROOT / 'src/renderer/occlusion_cull_pass.cpp'
INC = ROOT / 'src/proxy/motion_output_occlusion_cull_inc.h'
MOTION = ROOT / 'src/proxy/motion_output.cpp'


class OcclusionCullHost(unittest.TestCase):
    def test_core_driver(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        with tempfile.TemporaryDirectory(prefix='x3-occlusion-cull-') as d:
            exe = Path(d) / 'occlusion_cull_host'
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                            str(ROOT / 'verification/probe/occlusion_cull_host.cpp'), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
        failed = [line for line in run.stdout.splitlines() if line.startswith('CHECK') and line.endswith('FAIL')]
        self.assertEqual(failed, [])
        result = dict(re.findall(r'(\w+)=(\d+)', run.stdout.splitlines()[-1]))
        self.assertEqual(run.returncode, 0)
        self.assertEqual(result['failed'], '0')
        self.assertGreaterEqual(int(result['checks']), 45)

    def test_draw_site_order(self):
        text = MOTION.read_text()
        props = text.index('if (props_on_ && cull_small_prop(call, route)) return;')
        occlusion = text.index('if (occlusion_on_ && cull_occluded(call, route)) return;')
        jitter = text.index('if (jitter_active_ && (shadow_.vs_row || shadow_.vs_prepass)) apply_jitter(route);')
        scene = text.index('route.scene = true;')
        self.assertLess(scene, props)
        self.assertLess(props, occlusion)
        self.assertLess(occlusion, jitter)  # the test is issued before the draw's jitter, nothing bound yet

    def test_lifetime_under_the_accounting(self):
        motion = MOTION.read_text()
        inc = INC.read_text()
        self.assertIn('taa_call([&] { occlusion_pass_->before_reset(); }', motion)
        self.assertIn('if (occlusion_pass_) occlusion_pass_->after_reset(result);', motion)  # arms only, no device call
        self.assertIn('taa_call([&] { hr = occlusion_pass_->recreate(); }', inc)  # the queries come back under it
        self.assertIn('release_occlusion_cull();', motion)
        self.assertIn('taa_call([&] { hr = occlusion_pass_->attach(device_, native_); }', inc)
        self.assertIn('taa_call([&] { occlusion_pass_->detach(); }', inc)
        source = PASS_CPP.read_text()
        before = source[source.index('void OcclusionCullPass::before_reset'):source.index('void OcclusionCullPass::after_reset')]
        self.assertIn('release_queries();', before)
        self.assertIn('ring_.clear();', before)
        after = source[source.index('void OcclusionCullPass::after_reset'):source.index('HRESULT OcclusionCullPass::recreate')]
        self.assertNotIn('create_queries', after)  # creating there would escape the owner's reference accounting
        self.assertIn('D3DPOOL_MANAGED', source)  # the strip survives Reset; only the queries are recreated

    def test_pool_and_rows(self):
        core = (ROOT / 'src/proxy/occlusion_cull_core.h').read_text()
        self.assertRegex(core, r'pool_size = 1024, pool_per_frame = pool_size / 2;')
        inc = INC.read_text()

        def row(prefix):
            start = inc.index(f'log("{prefix} ')
            return inc[start:inc.index('id_,', start)]
        frame, session = row('occlusion_cull device=%llu frame=%llu'), row('occlusion_cull_session')
        for field in ('candidates', 'tested', 'hidden', 'skipped', 'ready', 'not_ready', 'drawn_late', 'pool_truncated'):
            self.assertTrue(re.search(rf'\b{field}=', frame), field)
            self.assertTrue(re.search(rf'\b{field}=', session), field)
        self.assertIn('log_tier::cached_debug &&', inc)  # the per-frame row: --debug only; the session row always

    def test_programs(self):
        source = PASS_CPP.read_text()
        words = [int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', source[source.index('rect_vs_words'):source.index('rect_ps_head')])]
        self.assertEqual(words[0], 0xfffe0300)  # vs_3_0
        self.assertEqual(words[-1], 0x0000ffff)
        self.assertIn(0x04000004, words)  # mad, four operand tokens
        self.assertEqual(words[words.index(0x04000004) + 1:words.index(0x04000004) + 5],
                         [0xe00f0000, 0x90e40000, 0xa0e400fd, 0xa0e400fc])  # o0, v0, c253, c252
        ps = [int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', source[source.index('rect_ps_head[]'):source.index('rect_elements')])]
        self.assertEqual(ps[:7], [0xffff0300, 0x05000051, 0xa00f0000, 0, 0, 0, 0])  # ps_3_0; def c0, 0, 0, 0, 0
        # Every output a bound target can receive is written 0 (the route's lazy RT1/RT2 stay bound between routed draws;
        # an unwritten output is undefined): mov oCt, c0 for each of the device's targets.
        self.assertIn('ps_words[n++] = 0x800f0800u | t;', source)
        self.assertIn('for (unsigned t = 0; t < targets_; ++t)', source)
        # The bound targets are read from the device per test (not a shadow: the HDR redirect), each must blend.
        self.assertIn('native_[GetRenderTarget]', source)
        self.assertIn('D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING', source)
        restore = source[source.index('// Restore, in reverse.'):]
        self.assertLess(restore.index('SetDeclFn'), restore.index('SetFvfFn'))  # the declaration before SetFVF
        self.assertIn('D3DRS_SRCBLEND, D3DBLEND_ZERO', source)
        self.assertIn('D3DRS_DESTBLEND, D3DBLEND_ONE', source)
        self.assertNotIn('D3DRS_COLORWRITEENABLE', source)  # a zero mask costs ~50 us per test on DXVK (gate.json)
        self.assertIn('GetData(&samples, sizeof samples, 0)', source)  # never a flush, never a stall
        self.assertNotIn('D3DGETDATA_FLUSH', source)

    def test_refusals_at_the_draw_site(self):
        inc = INC.read_text()
        self.assertIn('fill == D3DFILL_SOLID', inc)
        self.assertIn('small_prop_extent(call, extent_box, false)', inc)  # the current buffer revision only
        self.assertIn('reason=no_bounds_source', inc)
        self.assertIn('if (!candidates_requested_)', inc)
        props = (ROOT / 'src/proxy/motion_output_cull_small_props_inc.h').read_text()
        self.assertIn('if (!e && allow_stale && stale', props)
        self.assertNotIn('shadow_.rt0.format', inc)  # the pass reads the bound targets itself

    def test_schema_entry(self):
        import sys
        sys.path.insert(0, str(ROOT / 'tools/config'))
        import schema
        entries = {e['key']: e for e in schema.SETTINGS}
        self.assertIn('occlusion_cull', entries)
        e = entries['occlusion_cull']
        self.assertEqual((e['type'], e['choices'], e['builtin']), ('enum', ('on', 'off'), 'on'))
        self.assertNotIn('occlusion_cull_budget', entries)  # no tuned budget (user 2026-10-08): only the fixed pool
        self.assertIn('X3M_OCCLUSION_CULL', (ROOT / 'src/config/config_schema_inc.h').read_text())

    def test_gate_record(self):
        gate = json.loads((ROOT / 'verification/results/occlusion-cull/gate.json').read_text())
        self.assertTrue(gate['gate_passed'])
        dxvk = gate['backends']['dxvk']
        self.assertLess(dxvk['production_cpu_us_per_test_max'], 5.0)
        self.assertEqual(dxvk['wrong'], 0)
        self.assertEqual(dxvk['production_lag1_ready_fraction_min'], 1.0)
        self.assertGreater(dxvk['mask_variant_pipeline_us_per_test_max'], 26.0)  # why the test blends instead
        ns = sorted({row['n'] for row in dxvk['rows']})
        self.assertIn(128, ns)
        self.assertIn(512, ns)


if __name__ == '__main__':
    unittest.main()
