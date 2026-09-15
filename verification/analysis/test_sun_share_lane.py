"""Host contracts; no claim of D3D execution or coverage qualification."""
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from tools.analysis import analyze_motion_readback as readback

ROOT = Path(__file__).resolve().parents[2]

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
            self.assertIn('PASS checks=30 ', result.stdout)
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
        result = subprocess.run(['python3', str(ROOT/'tools/manage.py'), 'launch', '--sun-shadow-lane', '--dry-run'], text=True, capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--sun-shadow-lane requires', result.stderr)

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
            publication = 'sun_shadow_lane_frame device=1 frame=7 available=0 receiver_draws=2 untracked_writers=5 failed=1\n'
            refusals = ('sun_shadow_lane_refusals device=1 frame=7 untracked=5 unknown=0 feature=0 scene=0 unregistered=2 pair=0'
                        ' no_zwrite=1 blended=2 state=0 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=3 overflow=0\n')
            writer = ('sun_shadow_lane_writer device=1 frame=7 index=1 vs=53a0a641107ed76c ps=8759c7838bbc86c2 reason=blended gate=4'
                      ' registered=1 z=1 zwrite=1 z_known=1 declaration=0000000000000123 stride=24\n')
            log = root/'capture.log'
            log.write_text(publication+refusals+writer)
            report = analyze(log, root)
            row = report['frames'][0]
            self.assertEqual(row['untracked_writers'], 5)
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

    def test_live_evidence_rejects_missing_draws_faults_and_taa(self):
        from run_sun_share_live import validate
        import json
        def witness(work, case):
            rows=[]; publications=[]; readbacks=[]; masks=[]; histories=[]
            early=case in ('cutout_drop','alpha_mask')
            late=case in ('late_shader','bind')
            composition=case.startswith('composition');failed_coverage=case in ('composition_missing','composition_failed')
            for i in range(6):
                lane=not early and not (late and i==3) and not (case=='late_shader' and i>=4)
                available=lane and not (i==2 and (late or case=='untracked' or failed_coverage))
                expected_history=int(i not in ((0,2,3,4) if failed_coverage else (0,4)))
                histories.append(f'SUN_HISTORY frame={i} expected={expected_history} reference={expected_history} actual={expected_history} fixture_cut={int(i in (0,4))}')
                bad=failed_coverage and i==2
                draws=(0,1,1 if case=='composition_missing' else 2,2,0,1)[i]
                excluded=0 if bad else (0,768,1536,1536,0,768)[i]
                required=int(composition and draws>0 and not (bad and case=='composition_missing'))
                if composition:
                    masks.append(f'SUN_M frame={i} draws={draws} valid={int(not bad)} required={required} excluded={excluded} eligible={0 if bad else 3600-excluded} linear={1 if bad and case=="composition_failed" else 0 if bad else draws} exchanged={2 if bad and case=="composition_failed" else 0 if bad else draws} incomplete={int(bad and case=="composition_failed")} stopped={int(bad)} interleaved={int(i==3)}')
                rows.append(f'SUN_LIVE frame={i} step={i} lane={int(lane)} available={int(available)} drawn=3600 positive={3600 if lane and i!=1 else 0} zero={3600 if lane and i==1 else 0} fault={int(late and i==2)} history={int(i not in ((0,2,3,4) if failed_coverage else (0,4)))}')
                publications.append(f'sun_shadow_lane_frame frame={i} available={int(available)} owner={int(not bad)} exclusion_required={required} exclusion_valid={int(not bad)} failed={int(late and i==2)} receiver_draws={int(lane)} untracked_writers={int(case=="untracked" and i==2)}')
                readbacks.append(f'motion_output_taa_readback frame={i} file=taa_{i}.rgba16f width=64 height=64 result=00000000')
                data=struct.pack('<4e', .5,.25,.75,1)*4096
                (work/f'reference_taa_{i}.rgba16f').write_bytes(data)
                (work/'x3-modern-captures'/f'taa_{i}.rgba16f').write_bytes(data)
            if case=='untracked':
                publications.append('sun_shadow_lane_refusals frame=2 untracked=1 unknown=0 feature=0 scene=0 unregistered=0 pair=1 no_zwrite=0 blended=0 state=0 rows=0 geometry=0 no_depth=0 fade_arm=0 apply_failed=0 scope=0 history=0 read_failed=0 signatures=1 overflow=0')
                publications.append('sun_shadow_lane_writer frame=2 index=1 vs=53a0a641107ed76c ps=3874adb0f396a660 reason=pair gate=3 registered=1 z=1 zwrite=0 z_known=1 declaration=0000000000000001 stride=24')
            qualifications=[f'sun_shadow_lane_device qualified={int(not early)} reason=ok']*2
            if case=='late_shader': qualifications[1]='sun_shadow_lane_device qualified=0 reason=shader_cache'
            depth=f'sun_shadow_lane_depth qualified={int(not early)} detail=stage={"cutout_pass" if early else "history_r"} result={"80004005" if early else "00000000"} restore=00000000 checks={4 if early else 18}'
            text='\n'.join(rows+masks+histories+['RESET PASS','SUN_LIVE_PASS frames=6','RESULT PASS checks=1'])
            return text,'\n'.join(qualifications+[depth]*(1 if case=='late_shader' else 2)+publications+readbacks)
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder);(work/'x3-modern-captures').mkdir()
            for case in ('positive','cutout_drop','alpha_mask','late_shader','bind','untracked','composition','composition_missing','composition_failed'):
                text,trace=witness(work,case)
                json.dumps(validate(text,trace,work,case),allow_nan=False)
                for badtext,badtrace in ((text.replace('drawn=3600','drawn=0',1),trace),
                                         (text.replace('positive=3600','positive=0',1),trace) if case not in ('cutout_drop','alpha_mask') else (text,trace.replace('stage=cutout_pass','stage=history_r'))):
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
                if case=='untracked':
                    with self.assertRaises(AssertionError): validate(text,trace.replace('untracked_writers=1','untracked_writers=0'),work,case)
                    # Refusal diagnostics: the bucket line, its sum, the identifying bucket and one signature line are all required.
                    refusal_line=next(l for l in trace.splitlines() if l.startswith('sun_shadow_lane_refusals '))
                    writer_line=next(l for l in trace.splitlines() if l.startswith('sun_shadow_lane_writer '))
                    for badtrace in (trace.replace(refusal_line+'\n',''), trace.replace('pair=1 no_zwrite=0','pair=0 no_zwrite=1'),
                                     trace.replace('pair=1 no_zwrite=0','pair=1 no_zwrite=1'), trace.replace(writer_line+'\n',''),
                                     trace.replace('reason=pair gate=3','reason=no_zwrite gate=4'), trace.replace('zwrite=0 z_known=1','zwrite=1 z_known=1'),
                                     trace.replace(writer_line,writer_line+'\n'+writer_line.replace('frame=2','frame=1').replace('ps=3874adb0f396a660','ps=0000000000000042')),
                                     trace.replace(' scope=0 history=0 read_failed=0',''),
                                     trace.replace(writer_line,writer_line+'\n'+writer_line), trace.replace('frame=2 untracked=1','frame=1 untracked=1')):
                        with self.assertRaises(AssertionError): validate(text,badtrace,work,case)
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
