"""Negative witnesses for the consume-only live GPU evidence checker."""
from verification.analysis.retired_tests import load_tests  # retired feature: hidden from default discovery
import copy
import json
from pathlib import Path
import re
import unittest
from run_linear_material_live import compare_cases, validate_case, ELIGIBLE, PIXEL_PROGRAMS, VERTEX_PROGRAMS, BUMP_FRAMES, MATCHED, FRAME_COUNT, CORPUS_PAIRS, IMPLEMENTED_PROGRAMS, MOTION_FRAMES, UNKNOWN_FRAMES, UNKNOWN_PIXEL, ASTEROID_FRAMES, expected_rgb, LIVE_MATERIAL_PROGRAMS, LIVE_MATERIAL_OBJECTS, WRAP_REPRESENTATIVES, validate_wrap_case, compare_wrap_cases


from run_linear_material_live import (XT_PAIRS, XT_PIXELS, XT_MATERIAL_PROGRAMS, XT_REPAIRED_PROGRAMS,
    XT_MATERIAL_OBJECTS, XT_DEFAULT_VS, XT_BUMP_VS, XT_ALPHA, xt_schedule, xt_expected_wrap,
    validate_xt_case, compare_xt_cases)


def xt_report(material=True,depth=True,mode='perdraw'):
    active=[s for s in xt_schedule(material) if s['frame'] is not None]
    output=[f'RESULT PASS checks=123 restorations={2*len(active)} frames={len(active)} depth_written={len(active) if depth else 0} taa_reference_frames=0','RESET PASS','RESET PASS']
    trace=[f'motion_output_device depth={int(depth)} depth_reason={"ok" if depth else "fixture_motion_only"} rt_mode={mode}',f'motion_output_release held={20+XT_MATERIAL_OBJECTS*material} released=1']
    if material:
        trace += [f'linear_material_variant kind={stage} original={program} transform=0 create=00000000' for stage,program in sorted(XT_MATERIAL_PROGRAMS)]
        trace += [f'linear_material_xt_default_variant kind={stage} original={program} linear={linear} transform=0 create=00000000' for stage,program,linear in sorted(XT_REPAIRED_PROGRAMS)]
    sequence=0
    for s in xt_schedule(material):
        plan,pair,step,frame=s['plan'],s['pair'],s['step'],s['frame']
        if frame is None:output += [f'XT_SKIP plan={plan} pair={pair} step={step} reason=native_invalid_linkage'];continue
        unknown=pair==15;vs,ps=XT_PAIRS[pair] if not unknown else (XT_DEFAULT_VS,UNKNOWN_PIXEL)
        rgb=[.5,.25,.75] if unknown else [4**(1/2.2) if s['combined'] else 1]*3
        alpha=.625 if unknown else XT_ALPHA
        for draw in range(2):
            sequence+=1;values=','.join(map(str,xt_expected_wrap(s,depth,draw)))
            output += [f'XT_WRAP plan={plan} frame={frame} draw={draw} sequence={sequence} valid=1 result=00000000 values={values}']
            if s['transport']:
                output += [f'XT_TRANSPORT plan={plan} frame={frame} draw={draw} combined={int(material and draw==1)} alpha=alpha motion=motion depth={"depth" if depth else "0000000000000000"} perspective=0.125 pixels={2048 if draw else 0} max_error=0 rgb_range={.25 if draw else 0} highlight_low={17**-6:.9g} highlight_high=1']
        rgba=','.join(map(str,rgb+[alpha]));image='linear' if s['combined'] else 'ordinary'
        output += [f'XT_LIVE plan={plan} frame={frame} pair={pair} step={step} vs={vs} ps={ps} combined={int(s["combined"])} refusal={1 if unknown else 4 if plan<84 and step==2 else 0} matched={int(s["matched"])} rgba={rgba} alpha_hash=alpha image_hash={image}',f'MOTION_HASH frame={frame} motion=motion depth={"depth" if depth else "0000000000000000"}']
        routed=0 if unknown else 2
        trace += [f'motion_output_frame frame={frame} routed={routed} depth_routed={routed*depth} matched={2*s["matched"]} gate3={2 if unknown else 0} apply_failures=0 restore_failures=0 taa_resolved=0 rt_mode={mode}']
        if material:trace += [f'linear_material_frame frame={frame} routed={(1 if s["transport"] else 2)*s["combined"]} bump_routed={(1 if s["transport"] else 2)*(s["combined"] and pair<10)} refused={1 if s["transport"] else 2 if not unknown and not s["combined"] else 0} bind_failures=0']
    return '\n'.join(output),trace


def cases():
    result = {}
    for owner in (0, 1):
        for taa in (0, 1):
            for material in (0, 1):
                result[f'ownership{owner}-taa{taa}-material{material}'] = {
                    'temporal_hashes': [['motion', 'depth']] * FRAME_COUNT,
                    'native_hashes': ['native'] * FRAME_COUNT,
                    'held_references': 20 + material * LIVE_MATERIAL_OBJECTS,
                    'pixel_programs': list(PIXEL_PROGRAMS),
                    'vertex_programs': list(VERTEX_PROGRAMS),
                    'rgba': [expected_rgb(frame,material) + [.75] for frame in range(FRAME_COUNT)],
                }
    return result


def sample_report():
    output = [f'RESULT PASS checks=1 restorations=1 frames={FRAME_COUNT} depth_written={FRAME_COUNT} taa_reference_frames={FRAME_COUNT} motion_pixels={FRAME_COUNT} matched_pixels={len(MATCHED)}']
    trace = [f'linear_material_variant kind={stage} original={identifier} transform=0 create=00000000'
             for stage, identifier in sorted(LIVE_MATERIAL_PROGRAMS)]
    trace += [f'linear_material_xt_default_variant kind=vs original=494fe349b8bc12ec linear={linear} transform=0 create=00000000' for linear in (0,1)]
    trace += ['motion_output_release held=69 released=1']
    for frame in range(FRAME_COUNT):
        eligible = int(frame in ELIGIBLE)
        output += [f'LINEAR_LIVE frame={frame} combined={eligible} refusal={4 if frame in MOTION_FRAMES-ELIGIBLE else 0} vs={VERTEX_PROGRAMS[frame]} ps={PIXEL_PROGRAMS[frame]} rgba=1,1,1,0.75 native_hash=abcdef',
                   f'MOTION_HASH frame={frame} motion=1234 depth=5678']
        trace += [f'motion_output_frame frame={frame} routed={int(frame in MOTION_FRAMES)} depth_routed={int(frame in MOTION_FRAMES)} matched={int(frame in MATCHED)} gate3={int(frame in UNKNOWN_FRAMES)} apply_failures=0 restore_failures=0 taa_resolved=1',
                  f'linear_material_frame frame={frame} routed={eligible} refused={int(frame in MOTION_FRAMES and not eligible)} bind_failures=0 bump_routed={int(eligible and frame in BUMP_FRAMES)}']
    return '\n'.join(output), trace


def wrap_report(material=True,depth=True,rt_mode="perdraw"):
    output=[f'RESULT PASS checks=36 restorations=36 frames=18 depth_written={18 if depth else 0} taa_reference_frames=0']
    trace=[f'motion_output_device depth={int(depth)} depth_reason={"ok" if depth else "fixture_motion_only"} rt_mode={rt_mode}',f'motion_output_release held={20+LIVE_MATERIAL_OBJECTS*material} released=1']
    if material:
        trace += [f'linear_material_variant kind={kind} original={identifier} transform=0 create=00000000' for kind,identifier in sorted(LIVE_MATERIAL_PROGRAMS)]
        trace += [f'linear_material_xt_default_variant kind=vs original=494fe349b8bc12ec linear={linear} transform=0 create=00000000' for linear in (0,1)]
    for frame in range(18):
        representative,step=divmod(frame,6);_,_,source,temporal,scalars=WRAP_REPRESENTATIVES[representative]
        values=[0]*16
        if step!=4:
            values[1],values[2]=((11,6) if step in (1,3) else (5,10))
            values[source]=2 if step in (1,3) else 1;values[8]=15
        if depth:values[8]=0
        combined=material and step!=2
        if combined:
            values[1]=(values[1]&7)|(8 if values[source]&1 else 0)
            if scalars==2:values[2]=(values[2]&7)|(8 if values[source]&2 else 0)
        for draw in range(2):output += [f'MATERIAL_WRAP frame={frame} representative={representative} step={step} draw={draw} sequence={frame*2+draw+1} valid=1 result=00000000 combined={int(combined)} source={source} motion={temporal} scalars={scalars} values='+','.join(map(str,values))]
        output += [f'MATERIAL_WRAP_IMAGE frame={frame} rgba=1,2,3,0.5 alpha_hash=alpha native_hash={"combined" if combined else "native"}', f'MOTION_HASH frame={frame} motion=abc depth={"def" if depth else "0000000000000000"}']
        trace += [f'motion_output_frame frame={frame} routed=2 depth_routed={2*depth} matched={0 if step in (0,4) else 2} gate3=0 apply_failures=0 restore_failures=0 taa_resolved=0 rt_mode={rt_mode}']
        if material:trace += [f'linear_material_frame frame={frame} routed={2*combined} bump_routed={2*combined} refused={0 if combined else 2} bind_failures=0']
    return '\n'.join(output),trace


class LiveMaterialReportTests(unittest.TestCase):
    def test_consecutive_indexed_sources_have_distinct_keys_and_equal_geometry(self):
        source=(Path(__file__).resolve().parents[2]/'verification/probe/motion_output_fixture.cpp').read_text()
        wrap=source[source.index('        if(materialwrap){'):source.index('        auto unknown_controls=')]
        xt=source[source.index('    void run_xt_materials('):source.index('    // ---- FP16 HDR scene path')]
        for label,method in (('WRAP',wrap),('XT',xt)):
            with self.subTest(mode=label):
                self.assertRegex(method,r'CreateIndexBuffer\(12,0,D3DFMT_INDEX16')
                indices=[int(value) for value in re.search(r'const unsigned short triangle\[\]=\{([^}]+)\}',method).group(1).split(',')]
                self.assertEqual(indices,[0,1,2,0,1,2])
                self.assertEqual(indices[0:3],indices[3:6])
                self.assertEqual(method.count('for(unsigned draw_number=0;draw_number<2;++draw_number)'),1)
                self.assertEqual(method.count('DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,3*draw_number,1)'),1)
                self.assertNotIn('DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1)',method)

    def test_xt_full_schedule_and_twins(self):
        cases={}
        for depth in (False,True):
            for mode in ('perdraw','lazy'):
                for material in (False,True):
                    output,trace=xt_report(material,depth,mode)
                    result=validate_xt_case(output,trace,material,depth,mode)
                    self.assertEqual(result['frames'],92 if material else 65)
                    self.assertEqual(len(result['skipped_plans']),0 if material else 27)
                    cases[f'depth{int(depth)}-{mode}-material{int(material)}']=result
        compare_xt_cases(cases)
        for key in ('alpha_hash','image_hash','temporal_hashes'):
            changed=copy.deepcopy(cases);changed['depth1-lazy-material1']['samples']['62'][key]='changed'
            with self.assertRaises(AssertionError):compare_xt_cases(changed)

    def test_xt_missing_or_corrupt_evidence_rejected(self):
        output,trace=xt_report()
        for prefix in ('XT_LIVE plan=0 ','XT_WRAP plan=0 ','MOTION_HASH frame=0 ','XT_TRANSPORT plan=90 ','RESET PASS'):
            changed='\n'.join(line for line in output.splitlines() if not line.startswith(prefix))
            with self.assertRaises(AssertionError):validate_xt_case(changed,trace,True,True,'perdraw')
        for source,replacement in [('combined=1','combined=0'),('sequence=1 ','sequence=2 '),('valid=1','valid=0'),('result=00000000','result=80004005'),('rgba=2.0','rgba=9.0'),('matched=1','matched=0')]:
            if source not in output:continue
            with self.assertRaises(AssertionError):validate_xt_case(output.replace(source,replacement,1),trace,True,True,'perdraw')
        for prefix in ('linear_material_variant ','linear_material_xt_default_variant ','motion_output_release '):
            changed=[line for line in trace if not line.startswith(prefix)]
            with self.assertRaises(AssertionError):validate_xt_case(output,changed,True,True,'perdraw')
        changed=re.sub(r'rgba=[^,]+,','rgba=99,',output,count=1)
        with self.assertRaises(AssertionError):validate_xt_case(changed,trace,True,True,'perdraw')
        off,offtrace=xt_report(False)
        with self.assertRaises(AssertionError):validate_xt_case(off.replace('native_invalid_linkage','qualified_native'),offtrace,False,True,'perdraw')
        with self.assertRaises(AssertionError):validate_xt_case(off+'\nXT_LIVE plan=60 frame=90',offtrace,False,True,'perdraw')

    def test_xt_transport_corruption_rejected(self):
        output,trace=xt_report()
        for old,new in [('pixels=2048','pixels=0'),('pixels=2048','pixels=1024'),('max_error=0','max_error=1.1'),('rgb_range=0.25','rgb_range=0'),('highlight_high=1','highlight_high=0.1')]:
            with self.subTest(field=old),self.assertRaises(AssertionError):
                validate_xt_case(output.replace(old,new,1),trace,True,True,'perdraw')
        for key in ('alpha','motion','depth'):
            lines=output.splitlines()
            index=next(i for i,line in enumerate(lines) if line.startswith('XT_TRANSPORT plan=90 ') and 'draw=1 ' in line)
            lines[index]=re.sub(rf'{key}=[^ ]+',f'{key}=mismatch',lines[index])
            with self.subTest(key=key),self.assertRaises(AssertionError):
                validate_xt_case('\n'.join(lines),trace,True,True,'perdraw')

    def test_xt_live_cpp_inventory_and_skip_policy(self):
        root=Path(__file__).resolve().parents[2]
        source=(root/'verification/probe/motion_output_fixture.cpp').read_text()
        method=source[source.index('    void run_xt_materials('):source.index('    // ---- FP16 HDR scene path')]
        pixels=re.findall(r'"([0-9a-f]{16})"',re.search(r'xt_ps\[\]=\{([^}]+)\}',method).group(1))
        self.assertEqual(pixels,list(XT_PIXELS))
        self.assertIn('if(corrected&&!material)',method)
        self.assertLess(method.index('if(corrected&&!material)'),method.index('frame_begin();'))
        self.assertTrue('config.observe_native_wrap = materialwrap || materialxt || materialglass;' in source)
        self.assertIn('const unsigned refusal_stage=bump?5:4;',method)
        self.assertIn('compare(before,snapshot()',method)
        self.assertIn('before_ps,after_ps',method)
        self.assertIn('observed.sequence==sequence',method)
        self.assertIn('const float camera[3][4]={{1,0,0,-1},{0,1,0,1},{0,0,1,1.5f}};',method)
        self.assertIn('caller[6]=transport?2:',method)
        self.assertIn('std::pow(17.,-6.)',method)
        self.assertIn('pixels>W*H/4&&high-low>.001&&max_error<=1',method)

    def test_prior_258_frame_ids_are_preserved(self):
        self.assertEqual(FRAME_COUNT,322)
        self.assertEqual(PIXEL_PROGRAMS[256:258],[UNKNOWN_PIXEL]*2)
        self.assertEqual(len(CORPUS_PAIRS[116:]),32)
        for index,(vertex,pixel,_) in enumerate(CORPUS_PAIRS[116:]):
            self.assertEqual(VERTEX_PROGRAMS[258+index*2:260+index*2],[vertex]*2)
            self.assertEqual(PIXEL_PROGRAMS[258+index*2:260+index*2],[pixel]*2)
            self.assertNotIn(258+index*2,MATCHED);self.assertIn(259+index*2,MATCHED)

    def test_physical_wrap_cases_and_failures(self):
        controls={}
        for depth in (False,True):
            for material in (False,True):
                for mode in ('perdraw','lazy'):
                    output,trace=wrap_report(material,depth,mode)
                    result=validate_wrap_case(output,trace,material,depth,mode)
                    controls[f'depth{int(depth)}-{mode}-material{int(material)}']=result
        compare_wrap_cases(controls)
        for frame,index in ((0,1),(0,2),(1,1),(1,2),(2,1),(4,1),(6,2),(12,7),(12,8)):
            output,trace=wrap_report()
            lines=output.splitlines()
            for n,line in enumerate(lines):
                if line.startswith(f'MATERIAL_WRAP frame={frame} ') and 'draw=0 ' in line:
                    prefix,raw=line.split('values=');values=list(map(int,raw.split(',')));values[index]^=8;lines[n]=prefix+'values='+','.join(map(str,values));break
            with self.subTest(frame=frame,index=index),self.assertRaises(AssertionError):validate_wrap_case('\n'.join(lines),trace,True,True,"perdraw")
        output,trace=wrap_report()
        for old,new in (('valid=1','valid=0'),('result=00000000','result=80004005'),('sequence=1 ','sequence=2 ')):
            with self.assertRaises(AssertionError):validate_wrap_case(output.replace(old,new,1),trace,True,True,"perdraw")
        # A mislabeled lazy run and a single inconsistent device/frame row
        # must fail independently; dropping all18 caller comparisons must too.
        with self.assertRaises(AssertionError):validate_wrap_case(output,trace,True,True,"lazy")
        for prefix in ('motion_output_device ','motion_output_frame frame=7 '):
            changed=[line.replace('rt_mode=perdraw','rt_mode=lazy') if line.startswith(prefix) else line for line in trace]
            with self.assertRaises(AssertionError):validate_wrap_case(output,changed,True,True,"perdraw")
        for count in (18,35,37):
            with self.assertRaises(AssertionError):validate_wrap_case(output.replace('restorations=36',f'restorations={count}'),trace,True,True,"perdraw")
        changed=copy.deepcopy(controls);changed['depth0-lazy-material1']['alpha_hashes'][3]='changed'
        with self.assertRaises(AssertionError):compare_wrap_cases(changed)
        changed=copy.deepcopy(controls);changed['depth1-perdraw-material1']['native_hashes'][2]='changed'
        with self.assertRaises(AssertionError):compare_wrap_cases(changed)

    def test_wrap_observer_and_depth_override_are_fixture_only(self):
        root=Path(__file__).resolve().parents[2]
        source=(root/'src/proxy/capture.cpp').read_text()
        # Includes may precede the helper inside the fixture guard. Permit only
        # include directives there, so an intervening #endif/#else cannot make
        # this observer production-visible while satisfying the test.
        guarded_helper=re.search(r'#ifdef X3M_MOTION_OUTPUT_FIXTURE\n(?:#include "[^"\n]+"\n)*void fixture_observe_wrap',source)
        self.assertIsNotNone(guarded_helper)
        helper=source[guarded_helper.start():source.index('HRESULT WINAPI draw_primitive(')]
        self.assertIn('if (!ctx.fixture_observe_native_wrap) return;',helper)
        self.assertIn('ctx.get<GetState>(58)',helper)
        self.assertNotIn('device->GetRenderState',helper)
        self.assertNotIn('GetEnvironmentVariable',helper)
        self.assertTrue(helper.rstrip().endswith('#endif'))
        indexed=source[source.index('HRESULT WINAPI draw_indexed('):source.index('HRESULT WINAPI draw_up(')]
        invocation='if(route.submit){++ctx.fixture_emission_source_calls;fixture_observe_wrap(ctx,d);}'
        self.assertIn(invocation+'\n#endif',indexed)
        # Other fixture checkpoints may share this block. Its nearest opening
        # conditional must still be the fixture guard, without an intervening
        # #else/#endif that would expose the observer in production.
        conditional=re.findall(r'^\s*#(?:if|ifdef|ifndef|elif|else|endif)\b[^\n]*',indexed[:indexed.index(invocation)],re.MULTILINE)
        self.assertEqual(conditional[-1].strip(),'#ifdef X3M_MOTION_OUTPUT_FIXTURE')
        self.assertLess(indexed.index('fixture_observe_wrap(ctx,d)'),indexed.index('cpu.before_original()'))
        motion=(root/'src/proxy/motion_output.cpp').read_text()
        self.assertEqual(motion.count('X3M_FIXTURE_MOTION_DEPTH'),1)
        override=motion[motion.index('// Attach-only capability-subset'):motion.index('    depth_enabled_ = !std::strcmp(reason')]
        self.assertIn('depth_reason="fixture_motion_only";',override)
        self.assertTrue(override.rstrip().endswith('#endif'))

    def test_valid_control_twins(self):
        compare_cases(cases())
        output, trace = sample_report()
        self.assertEqual(validate_case(output, trace, True, True)['pixel_programs'], PIXEL_PROGRAMS)

    def test_complete_pair_inventory_and_history_schedule(self):
        self.assertEqual(len(CORPUS_PAIRS), 148)
        self.assertEqual(len({(v,p) for v,p,_ in CORPUS_PAIRS}), 148)
        self.assertEqual(len(IMPLEMENTED_PROGRAMS), 115)
        self.assertEqual(FRAME_COUNT, 322)
        for index, (vertex,pixel,bump) in enumerate(CORPUS_PAIRS):
            for repeat in range(2):
                frame = (24 if index<116 else 26) + index*2 + repeat
                self.assertEqual((VERTEX_PROGRAMS[frame],PIXEL_PROGRAMS[frame]), (vertex,pixel))
                self.assertEqual(frame in MATCHED, bool(repeat))
                self.assertEqual(frame in BUMP_FRAMES, bump)
        changed = cases()
        changed['ownership0-taa1-material1']['pixel_programs'][-1] = 'ef2bf556f207b8bd'
        with self.assertRaises(AssertionError): compare_cases(changed)
        output, trace = sample_report()
        trace = [line.replace('matched=1','matched=0') if line.startswith('motion_output_frame frame=243 ') else line for line in trace]
        with self.assertRaises(AssertionError): validate_case(output, trace, True, True)

    def test_cpp_corpus_matches_runner_and_proved_shapes(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'verification/probe/motion_output_fixture.cpp').read_text()
        source = source[source.index('    void run_linear_materials('):source.index('    // ---- FP16 HDR scene path')]
        vertices = re.findall(r'"([0-9a-f]{16})"', re.search(r'corpus_vs\[\]=\{([^}]+)\}', source).group(1))
        pixels = re.findall(r'"([0-9a-f]{16})"', re.search(r'corpus_ps\[\]=\{([^}]+)\}', source).group(1))
        table = re.search(r'const CorpusPair corpus\[\]=\{(.*?)\n        \};', source, re.S).group(1)
        rows = re.findall(r'\{(\d+),(\d+),(true|false),(true|false),(true|false),(true|false)\}', table)
        self.assertEqual(len(rows),148)
        report = json.loads((root / 'docs/reverse-engineering/linear-material-profiles.json').read_text())
        programs = {p['id']:p for p in report['programs']}
        for index,(v,p,bump,affine,standard,low) in enumerate(rows):
            vertex,pixel = vertices[int(v)],pixels[int(p)]
            self.assertEqual((vertex,pixel,bump=='true'),CORPUS_PAIRS[index])
            profile = programs['ps_'+pixel]
            self.assertEqual(affine=='true',bool(profile['diffuse_affine_completion']))
            self.assertEqual(standard=='true',profile['families'][0].startswith('standard'))
            self.assertEqual(low=='true',profile['families']==['standard_bump_low'])
        self.assertEqual(set(IMPLEMENTED_PROGRAMS),{tuple(p['id'].split('_',1)) for p in report['programs']})

    def test_valid_xt_and_unknown_controls_have_distinct_routes(self):
        for frame in (9,19):
            self.assertEqual((VERTEX_PROGRAMS[frame],PIXEL_PROGRAMS[frame]), ('37c34a7478544c14','5f82ecacd39529cd'))
            self.assertIn(frame,MOTION_FRAMES)
            self.assertIn(frame,ELIGIBLE)
            self.assertIn(frame,BUMP_FRAMES)
        for frame in UNKNOWN_FRAMES:
            self.assertEqual((VERTEX_PROGRAMS[frame],PIXEL_PROGRAMS[frame]),('53a0a641107ed76c',UNKNOWN_PIXEL))
            self.assertNotIn(frame,MOTION_FRAMES|ELIGIBLE|MATCHED)
        controls = cases()
        for case in controls.values(): case['rgba'][19][0] = float('nan')
        with self.assertRaises(AssertionError): compare_cases(controls)
        output,trace=sample_report()
        report=validate_case(output,trace,True,True)
        self.assertEqual(report['refused_frames'],[2,3,4,5,7,13,15])
        self.assertTrue(UNKNOWN_FRAMES.isdisjoint(report['refused_frames']))
        for old,new in [('routed=0 depth_routed=0','routed=1 depth_routed=1'),('matched=0','matched=1')]:
            changed=[line.replace(old,new) if line.startswith('motion_output_frame frame=256 ') else line for line in trace]
            with self.assertRaises(AssertionError): validate_case(output,changed,True,True)
        changed=cases();changed['ownership0-taa1-material1']['native_hashes'][256]='changed'
        with self.assertRaises(AssertionError): compare_cases(changed)

    def test_stdout_refusal_reason_schedule_is_checked(self):
        output,trace=sample_report()
        for frame,wrong in ((256,1),(257,1),(9,1),(19,4),(2,1),(7,0),(13,0),(15,1),(0,4),(244,1)):
            changed='\n'.join(re.sub(r'refusal=\d+',f'refusal={wrong}',line) if line.startswith(f'LINEAR_LIVE frame={frame} ') else line for line in output.splitlines())
            with self.subTest(frame=frame),self.assertRaises(AssertionError):
                validate_case(changed,trace,True,True)

    def test_unknown_pixel_has_only_supplied_color0_input(self):
        import struct
        source=(Path(__file__).resolve().parents[2]/'verification/probe/motion_output_fixture.cpp').read_text()
        text=re.search(r'const DWORD unknown_program\[\]=\{([^}]+)\}',source).group(1)
        words=[int(token.strip().rstrip('u'),16) for token in text.split(',')]
        value=14695981039346656037
        for byte in struct.pack('<%dI'%len(words),*words): value=((value^byte)*1099511628211)&((1<<64)-1)
        self.assertEqual(f'{value:016x}',UNKNOWN_PIXEL)
        self.assertEqual(words[7:10],[0x0200001f,0x8000000a,0x900f0000])
        self.assertEqual(words[10:],[0x02000001,0x800f0800,0xa0e40000,0x03000005,0x80080800,0xa0ff0000,0x90ff0000,0xffff])

    def test_asteroid_expected_colors_and_second_history_are_required(self):
        self.assertEqual(ASTEROID_FRAMES,set(range(244,256)))
        for frame in ASTEROID_FRAMES:
            self.assertEqual(frame in MATCHED,bool((frame-244)%2))
            self.assertNotEqual(expected_rgb(frame,True),expected_rgb(24,True))
        changed=cases();changed['ownership1-taa0-material1']['rgba'][244][1]=1.
        with self.assertRaises(AssertionError): compare_cases(changed)
        output,trace=sample_report()
        changed=[line.replace('matched=1','matched=0') if line.startswith('motion_output_frame frame=255 ') else line for line in trace]
        with self.assertRaises(AssertionError): validate_case(output,changed,True,True)

    def test_rejects_lost_temporal_color_alpha_or_references(self):
        base = cases()
        for failure in ('temporal', 'refcount', 'fallback', 'alpha', 'activation', 'shared_activation', 'shared_schedule', 'bump_activation', 'bump_refusal', 'bump_schedule', 'bump_reset', 'reset'):
            changed = copy.deepcopy(base)
            item = changed['ownership0-taa1-material1']
            if failure == 'temporal': item['temporal_hashes'][3] = ['wrong', 'depth']
            elif failure == 'refcount': item['held_references'] -= 1
            elif failure == 'fallback': item['native_hashes'][2] = 'changed'
            elif failure == 'alpha': item['rgba'][0][3] += .1
            elif failure == 'activation': item['rgba'][0][0] = 1.
            elif failure == 'shared_activation': item['rgba'][1][0] = 1.
            elif failure == 'shared_schedule': item['pixel_programs'][1] = '462342e3e5781384'
            elif failure == 'bump_activation': item['rgba'][12][0] = 1.
            elif failure == 'bump_refusal': item['rgba'][13][0] += .1
            elif failure == 'bump_schedule': item['vertex_programs'][12] = '53a0a641107ed76c'
            elif failure == 'bump_reset': item['rgba'][21][0] = 1.
            else: item['rgba'][10][0] = 1.
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                compare_cases(changed)
        output, trace = sample_report()
        for failure in ('missing_shared_variant', 'split_variant', 'split_admitted', 'bump_counter', 'bump_negative_admitted', 'missing_bump_variant'):
            changed = list(trace)
            if failure == 'missing_shared_variant': del changed[2]
            elif failure == 'split_variant': changed[2] = 'linear_material_variant kind=ps original=ef2bf556f207b8bd transform=0 create=00000000'
            elif failure == 'split_admitted': changed = [line.replace('routed=1 refused=0', 'routed=0 refused=1') if line.startswith('linear_material_frame frame=9 ') else line for line in changed]
            elif failure == 'bump_counter': changed = [line.replace('bump_routed=1', 'bump_routed=0') if line.startswith('linear_material_frame frame=12 ') else line for line in changed]
            elif failure == 'bump_negative_admitted': changed = [line.replace('routed=1 refused=0', 'routed=0 refused=1') if line.startswith('linear_material_frame frame=19 ') else line for line in changed]
            else: del changed[4]
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                validate_case(output, changed, True, True)


if __name__ == '__main__':
    unittest.main()
