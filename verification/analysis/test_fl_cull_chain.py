"""Host tests of the FL cull/LOD selector ports (src/engine/fl/cull_small_parts_fl.cpp, terran_station_lod_fl.cpp,
lod_occlusion_fl.cpp; cull_small_parts_sites_fl.h, lod_patch_sites_fl.h).

docs/verification/cull-small-parts.md, "FL port (2026-11-02, not flown)"; sites and ABI: docs/reverse-engineering/fl/
cull-chain.md, "Verified 2026-11-02 against X3FL.exe". (a) The native fixture verification/probe/fl_cull_chain.cpp
(MinGW i686, run natively through WSL interop) lays the three FL windows, the marker and JET writers into a 64 KB-aligned
private image, installs with the production modules and runs the real window bytes from the site with the FL selector's
frame: the stub's decisions for small, big, dock, projectile and far-jet nodes against the unpatched image (ECX live and
kept, EAX/EDX as the native max-threshold block leaves them at 0x0961f3, the native flags at both continuations, ESP/EBP,
locals, DF, LastError), the three stub variants, every single-bit window change refused (the occlusion fixup zeroed or
rebased too), read-back / chain / restore faults, shutdown, initialize()'s rows and refusals, the frame side, the two
one-byte patches' executed paths, and the late-window refusal. The emitted stubs are walked instruction by instruction
(verification/results/fl-cull-chain/stub_cpu_state_audit.py). The far-jet hand-off (cull-chain.md "Verified
2026-11-03"): the live stub's far block reaches the production handler (src/proxy/engine_far_jets.cpp, linked under a
renamed symbol with its own no-SSE flags) with FL-layout nodes and AP-offset decoys, and the copy becomes a plume record
through the consumer's far_record with the owned context scale. (b) Edition wiring: the CMake lists, the capture.cpp
un-fencing, the far-jet handler's FL read set, the plume stage's FL scale source and the FL constants against the
verified contract.json and read_set.json. The FL node walk (docs/architecture/fl-nozzle-walk-activation.md): the
fixture's node_walk section (owned scale, hook-only boundary, FL layout past AP decoys, unreadable child, emitter
setting, SETA probe clock) and the plume stage's FL wiring.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / 'src/engine/fl/cull_small_parts_fl.cpp'
SITES = ROOT / 'src/engine/fl/cull_small_parts_sites_fl.h'
LOD_SITES = ROOT / 'src/engine/fl/lod_patch_sites_fl.h'
FIXTURE = ROOT / 'verification/probe/fl_cull_chain.cpp'
SHIM = ROOT / 'verification/probe/fl_cull_chain_shim.h'
AUDIT = ROOT / 'verification/results/fl-cull-chain/stub_cpu_state_audit.py'
READ_SET = ROOT / 'verification/results/fl-far-jets/read_set.json'
CONTRACT = ROOT / 'verification/results/fl-cull-chain/contract.json'


def build_and_run(tmp):
    """Builds the fixture from the production sources, runs it natively and audits the emitted stubs. Returns (run,
    audit) CompletedProcess objects; None when the toolchain or interop is absent
    (verification/results/fl-cull-chain/mutation_check.py calls it over mutated sources)."""
    compiler = shutil.which('i686-w64-mingw32-g++')
    if not compiler or not Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists():
        return None
    tmp = Path(tmp)
    common = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
              '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-msse2', '-mfpmath=sse',
              '-DX3M_CULL_SMALL_PARTS_FL_FIXTURE', '-DX3M_LOD_PATCHES_FL_FIXTURE']
    objects = []
    units = (('src/engine/fl/cull_small_parts_fl.cpp',
              ['-fno-exceptions', '-include', str(SHIM), '-DX3M_CULL_SMALL_PARTS_FL_SHIM']),
             ('src/engine/fl/terran_station_lod_fl.cpp', ['-fno-exceptions']),
             ('src/engine/fl/lod_occlusion_fl.cpp', ['-fno-exceptions']),
             ('src/proxy/engine_patch.cpp', []), ('src/engine/fl/executable_identity_fl.cpp', []),
             # The production far-jet handler with its CMake flags, renamed so the fixture's recording fake forwards.
             ('src/proxy/engine_far_jets.cpp', ['-fno-exceptions', '-mno-sse', '-mno-mmx', '-mfpmath=387',
                                                '-Dx3m_engine_far_jet=x3m_engine_far_jet_real']),
             # The production reader of the FL node walk's section (its CMake flags).
             ('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']))
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *common, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / 'fl_cull_chain.exe'
    subprocess.run([compiler, *common, '-static', str(FIXTURE), *objects, '-o', str(exe)], check=True)
    run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=180)
    out = tmp / 'fixture.out'
    out.write_text(run.stdout)
    audit = subprocess.run(['python3', str(AUDIT), str(out)], text=True, capture_output=True)
    return run, audit


class FlCullChainNativeFixture(unittest.TestCase):
    def test_stub_patches_install_rollback_and_late_window(self):
        with tempfile.TemporaryDirectory(prefix='x3-fl-cull-') as tmp:
            outcome = build_and_run(tmp)
        if outcome is None:
            self.skipTest('needs i686-w64-mingw32-g++ and WSL interop (a native Windows run)')
        result, audit = outcome
        out = result.stdout
        self.assertEqual(result.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:4000]
                         + result.stderr[-1000:])
        self.assertRegex(out, r'RESULT PASS checks=\d+ failures=0')
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+)', out).group(1)), 150)
        self.assertRegex(out, r'WINDOWS cull_fnv1a=9df9cf9e terran_fnv1a=0e8e0d62 occlusion_fnv1a_masked=3105ccaa '
                              r'marker_store=7fd6e34b marker_or=147c2006 jet_writer=4d34a219 ')
        self.assertIn('FLIPS cull=440 refused=440', out)
        self.assertIn('FLIPS terran=248 refused=248 occlusion=160 refused=160 fixup=32 fixup_refused=32', out)
        self.assertIn('DECISIONS culled=20 dock=10 exempt=6 far=3 native=41', out)
        self.assertIn('FARCHAIN record origin=1000.0,-2000.0,3000.0 size=2.0000 scale=3c23d70a', out)
        for label in ('init_installs_far_block', 'init_jet_writer_mismatch_drops_far', 'far_chain_fl_layout_copied',
                      'far_chain_record_reaches_consumer', 'far_chain_foreign_context_refused',
                      'far_chain_null_context_refused', 'far_chain_model_is_fl_offset', 'far_chain_disarmed',
                      'init_far_gate_unset_on', 'init_far_gate_empty_on', 'init_far_gate_plumes_on',
                      'init_far_gate_native_off', 'init_far_gate_off_off', 'init_far_gate_mixed_case_off',
                      'init_far_gate_invalid_off'):
            self.assertNotIn('FAIL ' + label, out)
        # The FL node walk (fl-nozzle-walk-activation.md): the scale owned at the gather (one camera chain), nothing read
        # outside the hook, FL model offsets past AP decoys, the owned 0.01 with the camera page decommitted across a
        # frame boundary (the reader's region cache re-queries per frame; a same-frame decommit is not caught), B's child
        # decommitted the same way abandoning B only, the emitter setting, the SETA-scaled probe clock (120 / 12 frames,
        # 600 stopped), refused frames keeping the ship roots marked, and the report window's quantiles.
        for label in ('node_walk_gather_owns_scale', 'node_walk_decommit_across_frames',
                      'node_walk_camera_unreadable_at_append', 'node_walk_present_reads_nothing', 'node_walk_boundary_rule',
                      'node_walk_hook_fl_layout_owned_scale', 'node_walk_decommit_across_frames_abandons_root',
                      'node_walk_emitters_off_no_c_read', 'node_walk_emitters_on_fl_layout',
                      'node_walk_no_context_no_scale', 'node_walk_seta_probe_clock',
                      'node_walk_refused_frames_keep_ship_roots', 'node_walk_window_quantiles'):
            self.assertNotIn('FAIL ' + label, out)
        self.assertRegex(out, r'NODEWALK owned_scale=3c23d70a chains=1 hook_records=4 walked=3 unreadable=1 '
                              r'camera_reads_at_append=0 present_reads=0 no_boundary=2 anchors=2 '
                              r'origin=1000\.0,-2000\.0,3000\.0 seta_frames=12[01],1[23],600 refused_keep_roots=1 '
                              r'window=151,298,301\n')
        self.assertIn('LOG cull_small_parts_frame device=1 frame=77 px=4 ', out)
        self.assertRegex(out, r'LOG cull_small_parts_frame [^\n]* far_jets=on edition=fl\n')
        self.assertEqual(len(re.findall(r'^LOG cull_small_parts requested=\S+ px=\S+ patched=[01] edition=fl state=',
                                        out, re.M)), 16)  # 9 + the seven far-gate settings
        self.assertEqual(audit.returncode, 0, audit.stdout[-3000:] + audit.stderr[-1000:])
        variants = json.loads(audit.stdout)['variants']
        self.assertEqual({k: (v['calls'], v['exits']['continue'] > 0, v['exits']['cull'] > 0) for k, v in variants.items()},
                         {'full': (1, True, True), 'no_projectiles': (1, True, True), 'no_far': (0, True, True)})


def strip(text):
    return re.sub(r'//.*', '', text)


class FlCullChainWiring(unittest.TestCase):
    def test_constants_match_the_verified_contract(self):
        contract = json.loads(CONTRACT.read_text())
        windows = contract['windows']
        sites, lod = SITES.read_text(), LOD_SITES.read_text()
        self.assertEqual(windows['shared_cull_window']['fl']['fnv1a32'], '9df9cf9e')
        self.assertEqual(windows['shared_cull_window']['fl']['start_rva'], '0x0961c5')
        self.assertEqual(windows['shared_cull_window']['fl']['length'], 55)
        self.assertIn('window_fnv1a = 0x9df9cf9e', sites)
        for key, value in (('projectile_store', '7fd6e34b'), ('projectile_or', '147c2006'), ('jet_writer', '4d34a219')):
            self.assertEqual(windows[key]['fl']['fnv1a32'], value)
            self.assertIn('0x' + value, sites)
        self.assertEqual(windows['terran_root_flag']['fl']['fnv1a32'], '0e8e0d62')
        self.assertEqual(windows['lod_occlusion_gate']['fl']['fnv1a32'], '3105ccaa')
        self.assertEqual(windows['lod_occlusion_gate']['fl']['relocated_dword_rvas'], ['0x0e56bc'])
        self.assertEqual(windows['lod_occlusion_gate']['fl']['relocated_targets_rva'], ['0x1a9014'])
        self.assertIn('{0x095e82, 31, terran_bytes, 0, 0, 0x16, 0xc1, 0xc0, 0x0e8e0d62}', lod)
        self.assertIn('{0x0e56b1, 20, occlusion_bytes, 0x0b, 0x1a9014, 0x10, 0x6f, 0x00, 0x3105ccaa}', lod)
        self.assertEqual(contract['edits']['terran'], {'after': 0xc0, 'before': 0xc1, 'result': 'cmovl eax, eax',
                                                       'rva': 0x095e98})
        self.assertEqual(contract['edits']['occlusion']['rva'], 0x0e56c1)
        self.assertEqual((contract['edits']['occlusion']['before'], contract['edits']['occlusion']['after']), (0x6f, 0))
        for rva in ('0x0961d3', '0x0961dc', '0x0961f3', '0x049b1d', '0x04a807', '0x03deb3'):
            self.assertIn(rva, sites)
        self.assertIn('model_offset = 0x144', sites)

    def test_edition_lists_and_capture(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        ap = re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('cull_small_parts.cpp', linked)   # AP only now: the fl target links its own provider
        self.assertIn('src/proxy/engine_far_jets.cpp', linked)
        for source in ('cull_small_parts.cpp', 'terran_station_lod.cpp', 'lod_occlusion.cpp'):
            self.assertIn('src/proxy/' + source, ap)
        self.assertIn('target_sources(d3d9 PRIVATE src/engine/fl/cull_small_parts_fl.cpp src/engine/fl/terran_station_lod_fl.cpp\n'
                      '    src/engine/fl/lod_occlusion_fl.cpp)', cmake)
        capture = strip((ROOT / 'src/proxy/capture.cpp').read_text())
        start = capture.index('    engine_effects_patch::initialize();\n    \n')
        fl = capture[start:capture.index('#endif', start)]
        self.assertLess(fl.index('terran_station_lod::initialize();'), fl.index('lod_occlusion::initialize();'))
        self.assertLess(fl.index('lod_occlusion::initialize();'), fl.index('cull_small_parts::initialize();'))
        # The frame side: an #else branch beside each AP call (AP order untouched).
        for call in ('cull_small_parts::present(ctx.id, ctx.frame, ctx.capture);', 'cull_small_parts::begin_frame();',
                     'cull_small_parts::after_reset(p ? p->BackBufferWidth : 0u);',
                     'cull_small_parts::set_backbuffer_width(p->BackBufferWidth);'):
            self.assertEqual(capture.count(call), 2, call)
            second = capture.rindex(call)
            self.assertIn('#else', capture[second - 120:second])
            end = capture.index('#endif', second)  # the FL branch closes next; other FL providers may sit beside the call
            self.assertNotIn('#else', capture[second:end])
            self.assertNotIn('#if', capture[second:end])

    def test_provider_contract(self):
        code = strip(PROVIDER.read_text())
        self.assertNotIn('0x400000', code)
        self.assertNotIn('0x0047d2a2', code)
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('engine_patch::install_window_open()', code)
        self.assertIn('spec.length = S::site_length;', code)
        self.assertIn('if (const char* refused = S::plan(window)) return done(refused);', code)
        self.assertIn('return done(roll_back("readback_rolled_back"));', code)
        # Far jets on FL as on AP: plumes asks for the far block, a changed JET writer drops it.
        self.assertIn('applied = install_base(base, exempt, far_jets);', code)
        self.assertIn('far_jets_state_ = far_jets ? "on" : "off";', code)
        self.assertIn('if (far_jets && !bytes_match(base + S::jet_writer_rva, S::jet_writer, S::jet_writer_length)) {', code)
        self.assertIn('far_jets_state_ = "writer_mismatch";', code)
        self.assertNotIn('fenced_fl', code)
        self.assertIn('"cull_small_parts requested=%s px=%.4g patched=%u edition=fl state=%s reason=%s site=0x%08lx '
                      'rva=0x%06lx cull=0x%08lx write=%s', code)
        far = strip((ROOT / 'src/proxy/engine_far_jets.cpp').read_text())
        self.assertIn('constexpr unsigned model_at = layout::edition.model;', far)
        self.assertIn('word(node, model_at)', far)
        self.assertIn('node_context && node_context == r.context ? word(node_context, core::context_scale_offset) : 0u;',
                      far)
        layout = strip((ROOT / 'src/proxy/engine_node_layout.h').read_text())
        self.assertIn('constexpr Layout fl{0x18, 0x28, 0x144};', layout)
        self.assertIn('constexpr unsigned fl_context = 0x1c;', layout)
        # The plume stage on FL: the owned scale, no later context +0x2c read for far records.
        plumes = strip((ROOT / 'src/proxy/motion_output_engine_plumes_inc.h').read_text())
        append = plumes[plumes.index('void MotionOutput::engine_far_append()'):plumes.index('bool MotionOutput::engine_node_read(')]
        fl, ap = append.split('#else', 1)[0], append.split('#else', 1)[1]
        self.assertIn('const std::uint32_t* owned_scale = engine_far_jets::context_scale_bits();', fl)
        self.assertIn('ee::far_record(f, scale, body, entry, frame_, &record);', fl)
        self.assertNotIn('engine_far_context_scale', fl)
        self.assertIn('ee::far_record(f, engine_far_context_scale(f.context), body, entry, frame_, &record);', ap)

    def test_node_walk_on_fl(self):
        """The FL node walk's production wiring (fl-nozzle-walk-activation.md; the fixture's node_walk section mirrors it)."""
        plumes = strip((ROOT / 'src/proxy/motion_output_engine_plumes_inc.h').read_text())
        self.assertNotIn('fl_draw_only', plumes)
        self.assertNotIn('engine_nozzle_source_ = engine_nozzle::core::Source::draw;', plumes)
        gather = plumes[plumes.index('void MotionOutput::engine_node_gather('):plumes.index('void MotionOutput::engine_node_append()')]
        fl = gather[gather.index('#if X3M_EDITION_FL\n    const unsigned admitted'):]
        self.assertIn('engine_node_scales_.own(engine_node_roots_.order[admitted], std::uint32_t(camera), frame_, '
                      '&engine_node_read, nullptr);', fl)
        self.assertLess(fl.index('const DWORD error = GetLastError();'), fl.index('engine_node_scales_.own('))
        self.assertLess(fl.index('engine_node_scales_.own('), fl.index('SetLastError(error);'))
        append = plumes[plumes.index('void MotionOutput::engine_node_append()'):]
        append = append[:append.index('\n}\n')]
        # The boundary first: refused before the timer, the dedupe and any read.
        gate = append.index('if (!nz::walk_admitted(true, engine_node_boundary_)) {')
        self.assertLess(gate, append.index('QueryPerformanceCounter(&begin);'))
        self.assertLess(gate, append.index('nz::walk('))
        refused = append[gate:append.index('#endif', gate)]
        self.assertIn('++engine_node_no_boundary_;', refused)
        self.assertNotIn('engine_node_read', refused)
        # The ring's parents stay ship roots on a refused frame (review F1), before the refusal returns.
        self.assertLess(refused.index('engine_ship_roots_.mark(engine_ring_->parent[i], engine_ring_frame_);'),
                        refused.index('return;'))
        # The refusal row per kind, re-armed once per report window (review F5), and the window row on every path.
        self.assertIn('"engine_node_walk_refused device=%llu frame=%llu reason=no_boundary end=%s roots=%u refused=%u '
                      'edition=fl"', refused)
        self.assertIn('if (frame_ >= engine_node_refused_next_[kind]) {', refused)
        self.assertIn('engine_node_refused_next_[kind] = frame_ + nz::WalkWindow::frames;', refused)
        self.assertIn('engine_node_window_row(false, gathered_roots);', refused)
        self.assertTrue(append.rstrip().endswith('engine_node_window_row(true, false);\n#endif'))
        self.assertIn('"engine_node_walk_window device=%llu frame=%llu edition=fl appends=%u walked=%u refused=%u '
                      'roots_max=%u unreadable=%u no_scale=%u walk_us_p50=%.1f walk_us_p99=%.1f walk_us_max=%.1f '
                      'samples=%u"', plumes)
        window = plumes[plumes.index('void MotionOutput::engine_node_window_row('):]
        window = window[:window.index('\n}\n')]
        self.assertNotIn('cached_debug', window)  # any log tier
        # The owned scale at the append (FL), the camera memo only on AP.
        scale = append[append.index('const std::uint32_t camera = roots.camera[slot]'):]
        scale = scale[:scale.index('if (!(scale > 0.f))')]
        fl_scale, ap_scale = scale.split('#else', 1)
        self.assertIn('const float scale = engine_node_scales_.scale(slot);', fl_scale)
        self.assertNotIn('engine_node_context_scale', fl_scale)
        self.assertIn('engine_node_context_scale(camera)', ap_scale)
        # The model offset for every walk, before the anchors' options.
        self.assertLess(append.index('walk_options.model_offset = engine_node_layout::edition.model;'),
                        append.index('if (engine_anchors_on_) {'))
        self.assertIn('engine_anchor_clock_ms_ += nz::probe_step_ms(engine_clock_.last_step, seta_rate_);', append)
        # The hull rows after the root loop (no file write between two list reads).
        loop_start = append.index('for (unsigned r = 0; gathered && r < roots.count; ++r)')
        loop = append[loop_start:append.index('engine_ship_roots_.mark(', loop_start)]
        self.assertNotIn('log(', loop)
        self.assertIn('engine_anchor_hull_pending_[engine_anchor_hull_pending_count_++]', loop)
        # The boundary is set from this resolve's source around the stage's append, and reset after it.
        run = plumes[plumes.index('HRESULT MotionOutput::run_engine_plumes()'):]
        run = run[:run.index('    if (!plumes_ || !plumes_armed_')]
        self.assertIn('engine_node_boundary_ = counters_.hook_scene_end && counters_.taa.source == '
                      'unsigned(SceneEndSource::Hook)', run)
        self.assertLess(run.index('? engine_nozzle::core::Boundary::hook'), run.index('engine_node_append();'))
        self.assertLess(run.index('engine_node_append();'), run.index('engine_node_boundary_ = engine_nozzle::core::Boundary::present;'))
        # FL arms the stage on gathered roots (the walk runs only there).
        route = strip((ROOT / 'src/proxy/motion_output.cpp').read_text())
        self.assertIn('|| (engine_nozzle_source_ == engine_nozzle::core::Source::node && engine_node_roots_frame_ == frame_ &&',
                      route)
        header = strip((ROOT / 'src/proxy/motion_output.h').read_text())
        owned = header.index('engine_nozzle::core::OwnedScales engine_node_scales_;')
        self.assertEqual(header.rindex('#if', 0, owned), header.rindex('#if X3M_EDITION_FL', 0, owned))  # FL members only
        self.assertLess(owned, header.index('engine_nozzle::core::Boundary engine_node_boundary_ = '
                                            'engine_nozzle::core::Boundary::present;'))
        self.assertLess(header.index('engine_nozzle::core::Boundary engine_node_boundary_'), header.index('#endif', owned))
        stage = re.search(r'log\("engine_stage device=.*?"', plumes, re.S).group(0)
        self.assertIn('node_walk_us=%.1f node_no_boundary=%u node_unreadable_total=%llu', stage)
        self.assertIn('"engine_nozzle_source device=%llu setting=%s status=%s source=%s requested=%u bound=%u roots_max=%u '
                      'edition=%s rule=%s"', plumes)
        self.assertIn('"fl", engine_nozzle_source_ == engine_nozzle::core::Source::node ? "hook_only" : "off"', plumes)

    def test_read_set_matches_the_verified_bytes(self):
        read_set = json.loads(READ_SET.read_text())
        self.assertTrue(read_set['passed'])
        self.assertEqual(read_set['fl_sha256'], 'e7f3aad8d728e021')
        self.assertTrue(read_set['node_context_dominates_site'])
        self.assertEqual(read_set['copied_field_stores'], [])
        self.assertEqual(read_set['esi_writers_reaching_site'], [])
        self.assertIn('0x095e5e', read_set['esi_writers'])
        self.assertEqual(len(read_set['calls_between_context_load_and_site']), 4)
        self.assertGreaterEqual(read_set['anchors'], 47)
        offsets = {row['field']: (row['ap_offset'], row['fl_offset']) for row in read_set['table']}
        self.assertEqual(offsets['node model id'], ('0x140', '0x144'))
        self.assertEqual(offsets['node context (FL owned scale, new)'], (None, '0x1c'))
        for row in read_set['table']:
            self.assertTrue(all(a['ok'] for a in row['anchors']), row['field'])
            if row['ap_offset'] and row['field'] != 'node model id':
                self.assertEqual(row['ap_offset'], row['fl_offset'], row['field'])
        for name in ('terran_station_lod_fl.cpp', 'lod_occlusion_fl.cpp'):
            text = strip((ROOT / 'src/engine/fl' / name).read_text())
            self.assertIn('x3m::fl::module_base()', text)
            self.assertIn('engine_patch::install_window_open()', text)
            self.assertIn('edition=fl state=%s reason=%s', text)
            self.assertNotIn('WriteFile', text)


if __name__ == '__main__':
    unittest.main()
