"""Host contracts; no claim of D3D execution or coverage qualification."""
from pathlib import Path
import shutil
import struct
import os
import subprocess
import tempfile
import unittest
from tools.analysis import analyze_motion_readback as readback

ROOT = Path(__file__).resolve().parents[2]

# Launcher dry runs must not discover the checkout's voice decoder copy.
NO_REPO_DECODER = {**os.environ, 'X3M_VOICE_DECODER_REPO': ''}

class SunShareLane(unittest.TestCase):
    def test_host_contract(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder) / 'sun-share-host'
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT/'verification/probe/sun_share_host.cpp'),
                            str(ROOT/'src/renderer/material_motion.cpp'), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], text=True, capture_output=True, check=True)
            self.assertIn('PASS checks=41 ', result.stdout)
            print(result.stdout.strip())

    def test_depth_stride(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'depth.rg32f'
            path.write_bytes(struct.pack('<8f', -.0, -1, .625, .375, -1, 0, 1, 1))
            self.assertEqual(list(readback.load_r32f(path, 2, 2, 2)), [-.0, .625, -1, 1])
            self.assertEqual(readback.load_r32f(path, 2, 2, 2).tobytes(), struct.pack('<4f', -.0, .625, -1, 1))
            with self.assertRaises(readback.MalformedInput):
                readback.load_r32f(path, 2, 2)
            with self.assertRaises(readback.MalformedInput):
                readback.load_r32f(path, 2, 2, 3)

    def test_cli_default_and_requirements(self):
        result = subprocess.run(['python3', str(ROOT/'tools/manage.py'), 'launch', '--sun-shadow-lane', '--dry-run'], text=True, capture_output=True, env=NO_REPO_DECODER)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--sun-shadow-lane requires --motion-output --taa --hdr.', result.stderr)
        # The latch without linear materials (legacy-sun-application.md 4.1): the
        # lane is accepted on original shading; the apply needs the lane and the replay.
        base = ['python3', str(ROOT/'tools/manage.py'), 'launch', '--dry-run', '--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa', '--hdr', '--sun-shadow-lane']
        result = subprocess.run(base, text=True, capture_output=True, env=NO_REPO_DECODER)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"X3M_SUN_SHADOW_LANE": "1"', result.stdout)
        self.assertIn('"X3M_LINEAR_MATERIALS": "0"', result.stdout)
        self.assertIn('"X3M_SUN_SHADOW_APPLY": "0"', result.stdout)
        result = subprocess.run(base + ['--sun-shadow-apply'], text=True, capture_output=True, env=NO_REPO_DECODER)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--sun-shadow-apply requires --sun-shadow-lane --shadow-replay-depth.', result.stderr)
        result = subprocess.run(base + ['--shadow-replay-depth', '--sun-shadow-apply'], text=True, capture_output=True, env=NO_REPO_DECODER)
        self.assertEqual(result.returncode, 0, result.stderr)
        for line in ('"X3M_SUN_SHADOW_LANE": "1"', '"X3M_SHADOW_REPLAY_DEPTH": "1"', '"X3M_SUN_SHADOW_APPLY": "1"', '"X3M_LINEAR_MATERIALS": "0"'):
            self.assertIn(line, result.stdout)

    def test_latch_has_no_linear_material_prerequisite(self):
        # Source contract of the latch (legacy-sun-application.md 4.1-4.2): the
        # tested-opaque arm, the lane qualification and the capture-side gate no
        # longer require linear materials; the cutout identity is unconditional
        # and the exact cutout arm keeps its linear-material key.
        motion = (ROOT/'src/proxy/motion_output.cpp').read_text()
        # Three copies of the tested-opaque arm, all without linear_material_requested_:
        # the draw_state_ok gate, its UnmatchedReason::State mirror, and the
        # SunUntrackedReason::State mirror of the lane's refusal diagnostic
        # (route.sun_refusal). The mirrors must repeat the gate term for term.
        self.assertEqual(motion.count('(sun_lane_active_ && !(shadow_.cutout_pair && cutout_arm_active_) && test <= 1 && color != 0)'), 3)
        self.assertNotIn('sun_lane_active_ && linear_material_requested_', motion)
        self.assertIn('shadow_.cutout_pair = (linear_material_requested_ || sun_lane_requested_) && cutout::pair(shadow_.vs_hash, shadow_.ps_hash);', motion)
        self.assertIn('(test == 1 && color == 7 && shadow_.cutout_pair && linear_material_requested_ && (cutout_ok = cutout_draw_state()))', motion)
        self.assertIn('if (sun_lane_requested_ && !linear_material_requested_ && entry.variant && renderer::material_motion_pixel_writes_depth(*entry.row, depth_enabled_)) {', motion)
        self.assertIn('linear_material_original_sun_share_pixel_variant(', motion)
        lane = (ROOT/'src/proxy/sun_share_lane_inc.h').read_text()
        self.assertIn('if(!enabled_||!depth_enabled_||!taa_enabled_||!hdr_enabled_)break;', lane)
        self.assertNotIn('linear_material_requested_', lane)
        capture = (ROOT/'src/proxy/capture.cpp').read_text()
        self.assertIn('sun_lane_enabled=asked&&motion_output_requested&&taa_requested&&hdr_requested;', capture)
        self.assertIn('apply_asked&&sun_lane_enabled&&depth_asked&&enabled', capture)
        # Scene-end order at both sites: lane publication, replay, apply, then fog (AO removed, batch 5).
        hook = motion[motion.index('void MotionOutput::scene_end_hook'):]
        self.assertLess(hook.index('publish_shadow_replay_candidates();'), hook.index('run_sun_shadow_apply();'))
        self.assertLess(hook.index('run_sun_shadow_apply();'), hook.index('run_volumetric_fog();'))
        copy = motion[motion.index('publish_sun_lane("copy")'):]
        self.assertLess(copy.index('publish_shadow_replay_candidates();'), copy.index('run_sun_shadow_apply();'))
        self.assertLess(copy.index('run_sun_shadow_apply();'), copy.index('run_volumetric_fog();'))
        self.assertNotIn('ambient_occlusion', motion)

    def test_publication_and_coverage(self):
        from tools.analysis.analyze_sun_share_lane import analyze
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'depth.rg32f').write_bytes(struct.pack('<12f', .5,0, .5,.75, .5,-1, -1,0, .5,1, .5,float('nan')))
            (root/'coverage.rgba16f').write_bytes(b''.join(struct.pack('<4e', value,0,0,0) for value in (0,1,0,0,0,0)))
            publication = 'sun_shadow_lane_frame device=1 frame=7 available=1 receiver_draws=2 exclusion_required=1 exclusion_valid=1\n'
            depth = 'motion_output_depth_readback device=1 frame=7 file=depth.rg32f format=rg32f_row_major result=00000000 width=3 height=2\n'
            coverage = 'sun_shadow_lane_coverage_readback device=1 frame=7 file=coverage.rgba16f format=rgba16f_row_major result=00000000 width=3 height=2\n'
            log = root/'capture.log'
            log.write_text(publication+depth+coverage)
            row = analyze(log, root)['frames'][0]
            self.assertEqual((row['eligible_pixels'], row['zero_sun_pixels'], row['excluded_pixels'], row['invalid_share_pixels']), (2,1,1,2))
            for text in (publication+depth, publication+depth+coverage.replace('frame=7', 'frame=6'), publication.replace('available=1','available=0')+depth+coverage):
                log.write_text(text)
                row = analyze(log, root)['frames'][0]
                self.assertFalse(row['available'])
                self.assertEqual(row['eligible_pixels'], 0)
            # The receiver-depth option's wide RT2 (rgba32f, 16 B/px): the same .r/.g beside clip-w lanes give the same counts.
            (root/'depth.rgba32f').write_bytes(b''.join(struct.pack('<4f', d, s, 37000.0, 37000.0) for d, s in ((.5,0), (.5,.75), (.5,-1), (-1,0), (.5,1), (.5,float('nan')))))
            wide = depth.replace('depth.rg32f', 'depth.rgba32f').replace('rg32f_row_major', 'rgba32f_row_major')
            log.write_text(publication+wide+coverage)
            row = analyze(log, root)['frames'][0]
            self.assertEqual((row['eligible_pixels'], row['zero_sun_pixels'], row['excluded_pixels'], row['invalid_share_pixels'], row['rt2_lanes']), (2,1,1,2,4))
            log.write_text(publication+depth.replace('rg32f_row_major', 'rgba32f_row_major')+coverage)  # a label that does not match the bytes
            self.assertFalse(analyze(log, root)['frames'][0]['available'])

    def test_refusal_diagnostics_grammar(self):
        """Bucket line and capped writer signatures of the untracked-writer veto (diagnostics only)."""
        from tools.analysis.analyze_sun_share_lane import analyze, REASONS
        self.assertEqual(len(REASONS), 16)
        header = (ROOT/'src/renderer/sun_share_frame.h').read_text()
        self.assertIn(f'sun_untracked_reason_count = {len(REASONS)};', header)
        for index, name in enumerate(REASONS):
            self.assertIn(f'case {index}: return "{name}"' if index else 'default: return "unknown"', header)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            publication = 'sun_shadow_lane_frame device=1 frame=7 available=0 receiver_draws=2 untracked_writers=5 non_depth_writers=3 failed=1\n'
            refusals = ('sun_shadow_lane_refusals device=1 frame=7 untracked=5 unknown=0 feature=0 scene=0 unregistered=2 pair=0'
                        ' no_zwrite=1 blended=2 state=0 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=3 overflow=0\n')
            writer = ('sun_shadow_lane_writer device=1 frame=7 index=1 vs=53a0a641107ed76c ps=8759c7838bbc86c2 reason=blended gate=4'
                      ' registered=1 z=1 zwrite=1 z_known=1 declaration=0000000000000123 stride=24\n')
            log = root/'capture.log'
            log.write_text(publication+refusals+writer)
            report = analyze(log, root)
            row = report['frames'][0]
            self.assertEqual((row['untracked_writers'], row['non_depth_writers'], row['grammar_old']), (5, 3, False))
            log.write_text(publication.replace(' non_depth_writers=3', '')+refusals+writer)
            old = analyze(log, root)['frames'][0]
            self.assertEqual((old['non_depth_writers'], old['grammar_old'], old['untracked_writers']), (None, True, 5))
            log.write_text(publication+refusals+writer)
            self.assertEqual(row['untracked_reasons'], dict(zip(REASONS, (0,0,0,2,0,1,2,0,0,0,0,0,0,0,0,0))))
            self.assertEqual((row['writer_signatures'], row['writer_overflow']), (3, 0))
            self.assertIsNone(row['diagnostics_malformed'])
            self.assertEqual(report['refusal_frames'], 1)
            self.assertEqual(report['untracked_reason_totals']['blended'], 2)
            self.assertEqual(report['writers'], [dict(device=1, frame=7, vs='53a0a641107ed76c', ps='8759c7838bbc86c2', reason='blended',
                                                      gate=4, registered=1, z=1, zwrite=1, z_known=1, declaration='0000000000000123', stride=24)])
            for head, bad, flag in ((publication, refusals.replace('blended=2', 'blended=1'), 'refusal_buckets_mismatch'),
                                    (publication.replace('untracked_writers=5', 'untracked_writers=6'), refusals, 'refusal_total_mismatch'),
                                    (publication, refusals.split(' rows=')[0]+'\n', 'refusal_line_truncated')):
                log.write_text(head+bad+writer)
                row = analyze(log, root)['frames'][0]
                self.assertEqual(row['diagnostics_malformed'], flag)
                self.assertNotIn('untracked_reasons', row)
                self.assertEqual(row['reason'], 'frame_unavailable')
            # A malformed diagnostics line never drops the substantive analysis of an available frame.
            (root/'depth.rg32f').write_bytes(struct.pack('<4f', .5,0, .5,.75))
            available = publication.replace('available=0', 'available=1')
            depth = 'motion_output_depth_readback device=1 frame=7 file=depth.rg32f format=rg32f_row_major result=00000000 width=2 height=1\n'
            log.write_text(available+refusals.split(' rows=')[0]+'\n'+depth)
            row = analyze(log, root)['frames'][0]
            self.assertEqual((row['diagnostics_malformed'], row['available'], row['eligible_pixels'], row['zero_sun_pixels']), ('refusal_line_truncated', True, 2, 1))
            log.write_text(publication)
            row = analyze(log, root)['frames'][0]
            self.assertIsNone(row['untracked_reasons'])
            self.assertIsNone(row['diagnostics_malformed'])

    def test_corpus_invalid_exports(self):
        import os
        originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        if not originals.is_dir():
            self.skipTest('local original corpus unavailable')
        compiler = shutil.which('clang++') or shutil.which('c++')
        with tempfile.TemporaryDirectory() as folder:
            exe = Path(folder)/'sun-share-variants'
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(ROOT/'verification/probe/sun_share_variant_probe.cpp'),
                            str(ROOT/'src/renderer/material_motion.cpp'), str(ROOT/'src/renderer/linear_material.cpp'),
                            '-o', str(exe)], check=True)
            result = subprocess.run([str(exe), str(originals)], text=True, capture_output=True, check=True)
            self.assertIn('linear=108 xt=4', result.stdout)
            print(result.stdout.strip())

    def test_material_gpu_evidence_requires_pixels(self):
        import run_linear_material as runner
        cases = runner.sun_share_cases()
        self.assertEqual(len(cases), 216)
        self.assertEqual(len({runner.PAIRS[c['pair']][1] for c in cases}), 108)
        def report(positive=1):
            clear = 'SUN_MATERIAL_CLEAR pixels=256 drawn=0 valid=0 positive=0 zero=0\n'
            rows = []
            for case in cases:
                pos = positive if case['label']=='sun_source' else 0
                rows.append(f"SUN_MATERIAL id={case['id']} pair={case['pair']} drawn=1 valid=1 positive={pos} zero={1-pos} invalid=0 max_error=0\n")
            return clear+''.join(rows)+f'SUN_MATERIAL_PASS cases=216 drawn=216 valid=216 positive={108*positive} zero={216-108*positive} invalid=0 max_error=0\n'
        self.assertEqual(runner.validate_sun_share_report(report(), cases)['positive_pixels'], 108)
        with self.assertRaises(AssertionError):
            runner.validate_sun_share_report(report(0), cases)
        with self.assertRaises(AssertionError):
            runner.validate_sun_share_report(report().replace('id=1 ', 'id=0 '), cases)
        # A false zero-sun count from a zero clear must fail despite positive
        # material samples elsewhere and an otherwise complete report.
        with self.assertRaisesRegex(AssertionError, 'clear pixels entered acceptance'):
            runner.validate_sun_share_report(report().replace('drawn=0 valid=0 positive=0 zero=0', 'drawn=256 valid=256 positive=0 zero=256'), cases)
        # Per-case drawn coverage is mandatory, not merely aggregate coverage.
        missing = report().replace('id=1 pair=0 drawn=1 valid=1 positive=0 zero=1', 'id=1 pair=0 drawn=0 valid=0 positive=0 zero=0')
        missing = missing.replace('cases=216 drawn=216 valid=216 positive=108 zero=108', 'cases=216 drawn=215 valid=215 positive=108 zero=107')
        with self.assertRaisesRegex(AssertionError, 'no valid drawn samples'):
            runner.validate_sun_share_report(missing, cases)

    def test_cutout_selftest_has_nonvacuous_pixel_oracle(self):
        """Execute the authored DEF/MOV payloads and RGB mask on the host.

        Native alpha/depth behavior is owned by the live GPU fault cases; this
        rejects the original identical-payload positive control without D3D.
        """
        import re
        cpp = (ROOT/'src/proxy/motion_output.cpp').read_text()
        inc = (ROOT/'src/proxy/sun_share_lane_inc.h').read_text()
        body = re.search(r'self_test_depth_program\[\] = \{(.*?)\};', cpp, re.S).group(1)
        writer = [int(v, 16) for v in re.findall(r'0x([0-9a-f]+)u', body)]
        def program(name, base):
            result = base[:]
            for index, value in re.findall(rf'{name}\[(\d+)\]=(0x[0-9a-f]+|0);', inc):
                result[int(index)] = int(value, 0)
            return result
        alternate = program('alternate_code', writer)
        passing = program('passing_code', alternate)
        def output(words):
            f = lambda i: struct.unpack('<f', struct.pack('<I', words[i]))[0]
            return tuple(f(i) for i in (*range(3,7), *range(9,13), 15,16))
        old, source, reject = map(output, (writer, passing, alternate))
        expected = (.5,.75,.25,1., 7.,8.,9.,-1., .125,.75)
        masked = source[:3] + old[3:4] + source[4:]
        self.assertEqual(masked, expected)
        self.assertGreater(source[3], 1/255)
        self.assertNotEqual(source[3], old[3])
        for start, end in ((0,3), (4,8), (8,10)):
            self.assertNotEqual(source[start:end], old[start:end])
            self.assertNotEqual(reject[start:end], source[start:end])
        self.assertEqual(reject[3], 0)
        self.assertNotEqual(old, expected, 'drop-all-cutout must fail actual pixel oracle')
        self.assertNotEqual(source, expected, 'broken RGB alpha mask must fail pixel oracle')

    def test_stamp_runs_after_the_gain_finishers_and_before_the_jitter_restore(self):
        # The stamp restores the PS to the application's (shadow_.ps): it must run
        # once finish_source_gain / finish_hull_gain have put that program back, and
        # while the draw's jittered clip rows are still on the device.
        proxy=(ROOT/'src/proxy/motion_output.cpp').read_text()
        body=proxy[proxy.index('void MotionOutput::after_draw(MotionRoute& route, HRESULT result) noexcept {'):]
        order=[body.index(x) for x in ('if (route.source_gain) finish_source_gain(route);','if (route.hull_gain) finish_hull_gain(route);','stamped=sun_stamp_draw(route);','restore_jitter(route);')]
        self.assertEqual(order,sorted(order)); self.assertEqual(body.count('sun_stamp_draw(route)'),1)
        lane=(ROOT/'src/proxy/sun_share_lane_inc.h').read_text()
        self.assertIn('if (shadow_.ps && shadow_.ps_depth_out) return false;',lane)
        self.assertIn('{D3DRS_COLORWRITEENABLE3, 0}',lane)
        self.assertIn('entry.depth_out = renderer::pixel_program_writes_depth(',proxy)

    def test_live_evidence_rejects_missing_draws_faults_and_taa(self):
        from run_sun_share_live import validate
        import json
        def witness(work, case):
            rows=[]; publications=[]; readbacks=[]; masks=[]; histories=[]; frames=[]
            early=case in ('cutout_drop','alpha_mask')
            lane_off=case=='xt_state_lane_off'
            late=case in ('late_shader','bind')
            composition=case.startswith('composition');failed_coverage=case in ('composition_missing','composition_failed')
            for i in range(6):
                lane=not early and not lane_off and not (late and i==3) and not (case=='late_shader' and i>=4)
                refused_share=case=='original_share_refused'
                available=lane and not (i==2 and (late or case in ('untracked','cutout_pair','shadow_apply','unregistered_fault','unregistered_mid') or failed_coverage)) and not refused_share
                non_writers=int((case in ('effects','cutout_pair_bias') or failed_coverage) and i==2)
                original=case in ('original_lane','shadow_apply')
                expected_history=int(i not in ((0,2,3,4) if failed_coverage else (0,4)))
                histories.append(f'SUN_HISTORY frame={i} expected={expected_history} reference={expected_history} actual={expected_history} fixture_cut={int(i in (0,4) and not lane_off)}')
                bad=failed_coverage and i==2
                draws=(0,1,1 if case=='composition_missing' else 2,2,0,1)[i]
                excluded=0 if bad else (0,768,1536,1536,0,768)[i]
                required=int(composition and draws>0 and not (bad and case=='composition_missing'))
                if composition:
                    masks.append(f'SUN_M frame={i} draws={draws} valid={int(not bad)} required={required} excluded={excluded} eligible={0 if bad else 3600-excluded} linear={1 if bad and case=="composition_failed" else 0 if bad else draws} exchanged={2 if bad and case=="composition_failed" else 0 if bad else draws} incomplete={int(bad and case=="composition_failed")} stopped={int(bad)} interleaved={int(i==3)}')
                rows.append(f'SUN_LIVE frame={i} step={i} lane={int(lane)} available={int(available)} drawn={0 if lane_off else 3600} positive={3600 if lane and i!=1 else 0} zero={3600 if lane and i==1 else 0} fault={int(late and i==2)} history={int(i not in ((0,2,3,4) if failed_coverage else (0,4)))}')
                publications.append(f'sun_shadow_lane_frame frame={i} available={int(available)} owner={int(not bad)} exclusion_required={required} exclusion_valid={int(not bad)} failed={int((late and i==2) or refused_share)} receiver_draws={int(lane)} untracked_writers={3 if case in ("unregistered_fault","unregistered_mid") and i==2 else int(case in ("untracked","cutout_pair","shadow_apply") and i==2)} non_depth_writers={non_writers}'
                                    f' shadows={int(case=="shadow_apply")} cutout_opaque_routed={int(case=="original_lane" and i==2)} cutout_opaque_lane={int(case=="original_lane" and i==2)} cutout_opaque_refused=0 original_variants={2 if case=="original_lane" else int(original and not refused_share)} original_refused={int(refused_share)} original_refused_draws={int(refused_share)}'
                                    f' stamped={3 if case=="unregistered" and i==2 else 0} stamp_refused={3 if case in ("unregistered_fault","unregistered_mid") and i==2 else 0} stamped_prims={3 if case=="unregistered" and i==2 else 0}')
                if refused_share:
                    publications.append(f'original_fill_frame frame={i} fill=0.05 admitted=1')
                    rows.append(f'SUN_SHARE_REFUSED frame={i} refused_programs=1 refused_draws=1 fill_draws=1 failed=0')
                if case=='shadow_apply':
                    applied=i in (1,3,4,5)
                    publications.append(f'shadow_replay_depth frame={i} replayed={0 if i==0 else 2} skipped_lease={2 if i==0 else 0} skipped_state=0 skipped_caps=0 draws=2 us=40.0')
                    publications.append(f'sun_shadow_apply_frame frame={i} applied={int(applied)} skip_reason={"none" if applied else ("replay" if i==0 else "lane")} exponent=1.000000 us={70.0 if applied else 0.0} map={512 if applied else 0} result={"00000000" if applied else "00000001"} restore=00000001 stage=0')
                    rows.append(f'SUN_APPLY frame={i} step={i} applied={int(applied)} attempted=1 replayed={0 if i==0 else 2} expect_applied={int(applied)} inner={4096 if i>=3 else 0} band=0 changed={4096 if i>=3 else 0}')
                    (work/f'apply_mask_{i}.u8').write_bytes(bytes(4096))
                frames.append(f'motion_output_frame frame={i} routed={int(not lane_off)} gate4={int(lane_off)}')
                readbacks.append(f'motion_output_taa_readback frame={i} file=taa_{i}.rgba16f width=64 height=64 result=00000000')
                data=struct.pack('<4e', .5,.25,.75,1)*4096
                (work/f'reference_taa_{i}.rgba16f').write_bytes(data)
                (work/'x3-modern-captures'/f'taa_{i}.rgba16f').write_bytes(data)
            if case in ('untracked','shadow_apply'):
                publications.append('sun_shadow_lane_refusals frame=2 untracked=1 unknown=0 feature=0 scene=0 unregistered=0 pair=1 no_zwrite=0 blended=0 state=0 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=1 overflow=0')
                publications.append(f'sun_shadow_lane_writer frame=2 index=1 vs=53a0a641107ed76c ps=3874adb0f396a660 reason=pair gate=3 registered=1 z=1 zwrite=1 z_known=1 declaration=0000000000000001 stride=24 test=-1 mask=-1 srgb=-1 cutout_pair=0 arm={int(case=="untracked")}')
            if case in ('original_lane','shadow_apply'):
                publications.append('sun_shadow_original_variant original=8759c7838bbc86c2 transform=0 create=00000000 words=1568 depth=1 fill=0 share_applied=1')
            if case=='original_share_refused':
                publications.append('sun_shadow_original_variant original=8759c7838bbc86c2 transform=0 create=80004005 words=1568 depth=1 fill=0.05 share_applied=0')
            if case=='original_lane':
                publications.append('sun_shadow_original_variant original=63f96eba9eea7880 transform=0 create=00000000 words=1600 depth=1 fill=0 share_applied=1')
                rows.append('SUN_ORIGINAL_CUTOUT frame=2 ps=63f96eba9eea7880 test=1 ref=1 mask=7 routed=1 gate4=0 opaque_routed=1 opaque_lane=1 opaque_refused=0 untracked=0 variants=2 refused=0')
            if case=='shadow_apply':
                publications.append('sun_shadow_apply_mode requested=1 enabled=1 lane=1 replay=1 linear_materials=0')
                publications.append('sun_shadow_apply_device attached=1 reason=ok result=00000000 slots=220')
            if case in ('xt_state','xt_state_lane_off'): rows.extend(f'SUN_XT_STATE frame={i} test=1 ref=1 func=7 mask=7 lane={int(not lane_off)} routed={int(not lane_off)} gate4={int(lane_off)}' for i in range(6))
            if case=='effects': rows.append('SUN_EFFECTS frame=2 depth_write=0 blend=1')
            if case.startswith('unregistered'):
                fault=case!='unregistered'
                rows.append(f'SUN_UNREGISTERED frame=2 sign_pixels=406 stamped_pixels={0 if fault else 406} fault={int(fault)}')
                if fault:
                    publications.append('sun_shadow_lane_refusals frame=2 untracked=3 unknown=0 feature=0 scene=0 unregistered=3 pair=0 no_zwrite=0 blended=0 state=0 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=2 overflow=0')
                    publications.append('sun_shadow_lane_writer frame=2 index=1 vs=0000000000000002 ps=0000000000000003 reason=unregistered gate=3 registered=1 z=1 zwrite=1 z_known=1 declaration=0000000000000001 stride=24 test=-1 mask=-1 srgb=-1 cutout_pair=0 arm=1')
            if case=='cutout_pair_bias':
                rows.append('SUN_CUTOUT_BIAS frame=2 ps=63f96eba9eea7880 test=1 ref=1 mask=7 bias=-0.5 routed=1 gate4=0 untracked=0 stage_bias=00000000')
                rows.append('SUN_CUTOUT_OPAQUE frame=2 routed=1 lane=1 refused=1 no_zwrite=1 state=0 untracked=0')
            if case=='cutout_pair':
                rows.append('SUN_CUTOUT_PAIR frame=2 ps=63f96eba9eea7880 test=0 mask=7 gate4=1')
                publications.append('sun_shadow_lane_refusals frame=2 untracked=1 unknown=0 feature=0 scene=0 unregistered=0 pair=0 no_zwrite=0 blended=0 state=1 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=1 overflow=0')
                publications.append('sun_shadow_lane_writer frame=2 index=1 vs=53a0a641107ed76c ps=63f96eba9eea7880 reason=state gate=4 registered=1 z=1 zwrite=1 z_known=1 declaration=0000000000000001 stride=24 test=0 mask=7 srgb=0 cutout_pair=1 arm=1')
            if lane_off:
                text='\n'.join(rows+histories+['RESET PASS','SUN_LIVE_PASS frames=6','RESULT PASS checks=1'])
                return text,'\n'.join(frames+readbacks)
            qualifications=[f'sun_shadow_lane_device qualified={int(not early)} reason=ok']*2
            if case=='late_shader': qualifications[1]='sun_shadow_lane_device qualified=0 reason=shader_cache'
            depth=f'sun_shadow_lane_depth qualified={int(not early)} detail=stage={"cutout_pass" if early else "history_r"} result={"80004005" if early else "00000000"} restore=00000000 checks={4 if early else 18}'
            text='\n'.join(rows+masks+histories+['RESET PASS','SUN_LIVE_PASS frames=6','RESULT PASS checks=1'])
            return text,'\n'.join(qualifications+[depth]*(1 if case=='late_shader' else 2)+publications+readbacks+frames)
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder);(work/'x3-modern-captures').mkdir()
            for case in ('positive','cutout_drop','alpha_mask','late_shader','bind','untracked','composition','composition_missing','composition_failed','xt_state','effects','xt_state_lane_off','cutout_pair','cutout_pair_bias','original_lane','shadow_apply','original_share_refused','unregistered','unregistered_fault','unregistered_mid'):
                text,trace=witness(work,case)
                report=validate(text,trace,work,case)
                json.dumps(report,allow_nan=False)
                if case=='unregistered':
                    # The stamp witness is exact: an unstamped draw, a pixel the stamp missed,
                    # a vetoed frame or a stamp restoration failure fails.
                    with self.assertRaises(AssertionError): validate(text,trace.replace('stamped=3','stamped=2'),work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('stamped_prims=3','stamped_prims=2'),work,case)
                    with self.assertRaises(AssertionError): validate(text.replace('stamped_pixels=406','stamped_pixels=405'),trace,work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('frame=2 available=1','frame=2 available=0'),work,case)
                    with self.assertRaises(AssertionError): validate(text,trace+'\nmotion_output_restore_failed frame=2 index=3 result=80004005 what=sun_stamp',work,case)
                if case in ('unregistered_fault','unregistered_mid'):
                    # The refused stamp keeps the veto: an available frame or a stamped pixel fails.
                    with self.assertRaises(AssertionError): validate(text,trace.replace('frame=2 available=0','frame=2 available=1'),work,case)
                    with self.assertRaises(AssertionError): validate(text.replace('stamped_pixels=0','stamped_pixels=1'),trace,work,case)
                if case=='positive':
                    with self.assertRaises(AssertionError): validate(text,trace.replace('stamped=0','stamped=1',1),work,case)
                if case=='original_lane':
                    # The cutout admission witness is required and exact; a converted
                    # material line anywhere in the trace fails the original case.
                    with self.assertRaises(AssertionError): validate(text.replace('SUN_ORIGINAL_CUTOUT frame=2','SUN_ORIGINAL_CUTOUT frame=3'),trace,work,case)
                    with self.assertRaises(AssertionError): validate(text.replace('opaque_lane=1','opaque_lane=0'),trace,work,case)
                    with self.assertRaises(AssertionError): validate(text,trace+'\nlinear_material_variant kind=ps original=8759c7838bbc86c2 transform=0',work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('share_applied=1','share_applied=0',1),work,case)
                if case=='original_share_refused':
                    # The fail-closed witness is exact: a frame published available, a dropped fill or an uncounted draw fails.
                    with self.assertRaises(AssertionError): validate(text,trace.replace('frame=3 available=0','frame=3 available=1'),work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('original_fill_frame frame=3 fill=0.05 admitted=1','original_fill_frame frame=3 fill=0.05 admitted=0'),work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('original_refused_draws=1','original_refused_draws=0',1),work,case)
                if case=='shadow_apply':
                    self.assertEqual((report['apply_frames'],report['apply_skipped'],report['exact_taa_frames']),(4,{0:'replay',2:'lane'},6))  # the synthetic witness is byte-identical on every frame; the live run reports 3
                    # A quad drawn on the map-less or lane-less frame, a missing
                    # skip, or a shadowed frame without a darkened footprint fails.
                    with self.assertRaises(AssertionError): validate(text,trace.replace('frame=0 applied=0 skip_reason=replay','frame=0 applied=1 skip_reason=none'),work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('frame=2 applied=0 skip_reason=lane','frame=2 applied=0 skip_reason=none'),work,case)
                    with self.assertRaises(AssertionError): validate(text.replace('inner=4096 band=0 changed=4096','inner=4096 band=0 changed=0',1),trace,work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('exponent=1.000000','exponent=0.454545'),work,case)
                    data=(work/'x3-modern-captures'/'taa_3.rgba16f').read_bytes()
                    (work/'x3-modern-captures'/'taa_3.rgba16f').write_bytes(struct.pack('<4e',.5,.25,.75,1)*4095+struct.pack('<4e',.5,.25,.75,.5))
                    with self.assertRaises(AssertionError): validate(text,trace,work,case)
                    (work/'x3-modern-captures'/'taa_3.rgba16f').write_bytes(data)
                for badtext,badtrace in (((text.replace('drawn=0','drawn=3600',1),trace),(text.replace('lane=0 available=0','lane=1 available=1',1),trace)) if case=='xt_state_lane_off' else
                                         ((text.replace('drawn=3600','drawn=0',1),trace),
                                          (text.replace('positive=3600','positive=0',1),trace) if case not in ('cutout_drop','alpha_mask','original_share_refused') else (text,trace.replace('stage=cutout_pass','stage=history_r')) if case!='original_share_refused' else (text,trace.replace('failed=1','failed=0',1)))):
                    with self.assertRaises(AssertionError): validate(badtext,badtrace,work,case)
                if case in ('cutout_drop','alpha_mask'):
                    with self.assertRaises(AssertionError): validate(text,trace.replace('result=80004005','result=00000000'),work,case)
                if case in ('late_shader','bind') or case.startswith('composition'):
                    with self.assertRaises(AssertionError): validate(text.replace('reference=1 actual=1','reference=1 actual=0',1),trace,work,case)
                if case in ('late_shader','bind'):
                    with self.assertRaises(AssertionError): validate(text,trace.replace('failed=1','failed=0'),work,case)
                    if case=='late_shader':
                        with self.assertRaises(AssertionError): validate(text,trace.replace('qualified=0 reason=shader_cache','qualified=1 reason=ok'),work,case)
                        with self.assertRaises(AssertionError): validate(text,trace.replace('reason=shader_cache','reason=ok'),work,case)
                    else:
                        with self.assertRaises(AssertionError): validate(text,trace.replace('qualified=1 reason=ok','qualified=0 reason=shader_cache',1),work,case)
                if case=='cutout_pair_bias':
                    # The admission telemetry is required and exact: a missing
                    # line, an unwritten lane share or a lost refusal bucket fails.
                    for bad in (text.replace('SUN_CUTOUT_OPAQUE frame=2 routed=1 lane=1 refused=1 no_zwrite=1 state=0 untracked=0\n',''),
                                text.replace('routed=1 lane=1','routed=1 lane=0'),
                                text.replace('refused=1 no_zwrite=1','refused=1 no_zwrite=0')):
                        with self.assertRaises(AssertionError): validate(bad,trace,work,case)
                if case=='untracked':
                    with self.assertRaises(AssertionError): validate(text,trace.replace('untracked_writers=1','untracked_writers=0'),work,case)
                    # Refusal diagnostics: the bucket line, its sum, the identifying bucket and one signature line are all required.
                    refusal_line=next(l for l in trace.splitlines() if l.startswith('sun_shadow_lane_refusals '))
                    writer_line=next(l for l in trace.splitlines() if l.startswith('sun_shadow_lane_writer '))
                    for badtrace in (trace.replace(refusal_line+'\n',''), trace.replace('pair=1 no_zwrite=0','pair=0 no_zwrite=1'),
                                     trace.replace('pair=1 no_zwrite=0','pair=1 no_zwrite=1'), trace.replace(writer_line+'\n',''),
                                     trace.replace('reason=pair gate=3','reason=no_zwrite gate=4'), trace.replace('zwrite=1 z_known=1','zwrite=0 z_known=1'),
                                     trace.replace('untracked_writers=1 non_depth_writers=0','untracked_writers=1 non_depth_writers=1'),
                                     trace.replace(writer_line,writer_line+'\n'+writer_line.replace('frame=2','frame=1').replace('ps=3874adb0f396a660','ps=0000000000000042')),
                                     trace.replace(' scope=0 history=0 read_failed=0',''),
                                     trace.replace(writer_line,writer_line+'\n'+writer_line), trace.replace('frame=2 untracked=1','frame=1 untracked=1')):
                        with self.assertRaises(AssertionError): validate(text,badtrace,work,case)
                elif case=='xt_state':
                    # The XT-state receiver must be tracked: any refusal line, a missing receiver or an old build without the non-writer field fails.
                    for badtrace in (trace+'\nsun_shadow_lane_refusals frame=2 untracked=1 unknown=0 feature=0 scene=0 unregistered=0 pair=0 no_zwrite=0 blended=0 state=1 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=1 overflow=0',
                                     trace.replace('receiver_draws=1','receiver_draws=0',1), trace.replace(' non_depth_writers=0','',1)):
                        with self.assertRaises(AssertionError): validate(text,badtrace,work,case)
                    with self.assertRaises(AssertionError): validate(text.replace('SUN_XT_STATE frame=2 test=1','SUN_XT_STATE frame=2 test=0'),trace,work,case)
                elif case=='xt_state_lane_off':
                    # Lane off: the same draws must not route (routed=0, gate4=1) and no lane line may appear.
                    for badtrace in (trace.replace('routed=0 gate4=1','routed=1 gate4=0',1), trace+'\nsun_shadow_lane_frame frame=2 available=0 receiver_draws=0 untracked_writers=0 non_depth_writers=0 failed=0 owner=1 exclusion_required=0 exclusion_valid=0'):
                        with self.assertRaises(AssertionError): validate(text,badtrace,work,case)
                    with self.assertRaises(AssertionError): validate(text.replace('lane=0 routed=0 gate4=1','lane=0 routed=1 gate4=0',1),trace,work,case)
                elif case=='cutout_pair':
                    # The cutout pair must stay a state refusal at gate 4; an admitted pair (no refusal) or another bucket fails.
                    refusal_line=next(l for l in trace.splitlines() if l.startswith('sun_shadow_lane_refusals '))
                    for badtrace in (trace.replace(refusal_line+'\n','').replace('untracked_writers=1','untracked_writers=0'), trace.replace('state=1','state=0').replace('pair=0 no_zwrite','pair=1 no_zwrite'),
                                     trace.replace('reason=state gate=4','reason=pair gate=3'),
                                     # New writer grammar: the gate-4 values read, the pair identity and the exact-arm latch are required.
                                     trace.replace(' test=0 mask=7 srgb=0 cutout_pair=1 arm=1',''), trace.replace('cutout_pair=1 arm=1','cutout_pair=1 arm=0'),
                                     trace.replace('test=0 mask=7','test=-1 mask=-1')):
                        with self.assertRaises(AssertionError): validate(text,badtrace,work,case)
                elif case=='cutout_pair_bias':
                    # Run 28 session B: the cutout pair under a nonzero mip bias must be tracked (routed, no gate-4 refusal, no
                    # untracked writer); the run81 pattern (a state refusal and signature on the pair) or a lost witness fails.
                    run81=('\nsun_shadow_lane_refusals frame=2 untracked=1 unknown=0 feature=0 scene=0 unregistered=0 pair=0 no_zwrite=0 blended=0 state=1 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=1 overflow=0'
                           '\nsun_shadow_lane_writer frame=2 index=1 vs=53a0a641107ed76c ps=63f96eba9eea7880 reason=state gate=4 registered=1 z=1 zwrite=1 z_known=1 declaration=0000000000000001 stride=24 test=1 mask=7 srgb=0 cutout_pair=1 arm=0')
                    frame2='sun_shadow_lane_frame frame=2 available=1 owner=1 exclusion_required=0 exclusion_valid=1 failed=0 receiver_draws=1 untracked_writers=0'
                    assert frame2 in trace
                    for badtext,badtrace in ((text.replace('step=2 lane=1 available=1','step=2 lane=1 available=0'),trace.replace(frame2,frame2.replace('available=1','available=0').replace('untracked_writers=0','untracked_writers=1'))+run81),
                                             (text,trace+run81), (text.replace('routed=1 gate4=0','routed=0 gate4=1'),trace), (text.replace('untracked=0','untracked=1'),trace),
                                             (text.replace('SUN_CUTOUT_BIAS frame=2','SUN_CUTOUT_BIAS_LOST frame=2'),trace), (text.replace('stage_bias=00000000','stage_bias=bf000000'),trace)):
                        with self.assertRaises(AssertionError): validate(badtext,badtrace,work,case)
                elif case=='effects':
                    # The non-depth effects draw must be counted without a veto: dropping the count or a refusal line for it fails.
                    for badtrace in (trace.replace('non_depth_writers=1','non_depth_writers=0'),
                                     trace.replace('untracked_writers=0 non_depth_writers=1','untracked_writers=1 non_depth_writers=1')):
                        with self.assertRaises(AssertionError): validate(text,badtrace,work,case)
                    with self.assertRaises(AssertionError): validate(text.replace('SUN_EFFECTS frame=2 depth_write=0','SUN_EFFECTS frame=2 depth_write=1'),trace,work,case)
                elif case=='positive':
                    with self.assertRaises(AssertionError): validate(text,trace+'\nsun_shadow_lane_refusals frame=2 untracked=1 unknown=1 feature=0 scene=0 unregistered=0 pair=0 no_zwrite=0 blended=0 state=0 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=1 overflow=0',work,case)
                if case.startswith('composition'):
                    for before,after in (('excluded=768','excluded=0'),('eligible=2064','eligible=3600'),('interleaved=1','interleaved=0'),('exchanged=2','exchanged=0')):
                        with self.assertRaises(AssertionError): validate(text.replace(before,after),trace,work,case)
                    with self.assertRaises(AssertionError): validate(text,trace.replace('exclusion_required=1','exclusion_required=0'),work,case)
                    if case!='composition':
                        with self.assertRaises(AssertionError): validate(text.replace('valid=0','valid=1'),trace,work,case)
                target=work/'x3-modern-captures/taa_3.rgba16f'
                target.write_bytes(struct.pack('<4e', .25,.25,.75,1)*4096)
                with self.assertRaises(AssertionError): validate(text,trace,work,case)

    def test_actual_target_replacement_preserves_only_compatible_rows(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler)
        source = (ROOT/'src/proxy/motion_output.cpp').read_text()
        def method(signature):
            start = source.index(signature)
            end = source.index('{', start)
            depth = 1
            cursor = end + 1
            while depth:
                depth += (source[cursor] == '{') - (source[cursor] == '}')
                cursor += 1
            return source[start:cursor]
        actual = '\n'.join(method(s) for s in ('void MotionOutput::release_target()', 'bool MotionOutput::ensure_target('))
        template = (ROOT/'verification/probe/sun_share_target_host.cpp').read_text()
        with tempfile.TemporaryDirectory() as folder:
            cpp=Path(folder)/'target.cpp';exe=Path(folder)/'target'
            cpp.write_text(template.replace('/* MOTION_OUTPUT_METHODS */', actual))
            subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT),str(cpp),str(ROOT/'src/renderer/motion_row_history.cpp'),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('SUN_TARGET_HOST_PASS checks=49 ',result.stdout)
            print(result.stdout.strip())
