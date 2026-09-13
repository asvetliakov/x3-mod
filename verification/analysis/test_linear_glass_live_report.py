"""Failure witnesses for the bounded live glass report and its input scope."""
import copy
from pathlib import Path
import unittest
import linear_glass_live_reference as ref


def report(material=True,depth=True,mode='perdraw'):
    output=[f'RESULT PASS checks=123 restorations=54 frames=27 depth_written={100 if depth else 0} taa_reference_frames=0','RESET PASS']
    trace=[f'motion_output_device depth={int(depth)} depth_reason={"ok" if depth else "fixture_motion_only"} rt_mode={mode}',f'motion_output_release held={20+len(ref.MATERIAL_PROGRAMS)*material} released=1']
    if material: trace += [f'linear_material_variant kind={stage} original={program} transform=0 create=00000000' for stage,program in sorted(ref.MATERIAL_PROGRAMS)]
    for s in ref.schedule(material):
        p=s['plan'];vs,ps=ref.PAIRS[s['pair']]
        depth_hash='depth' if depth else '0000000000000000'
        for draw in (0,1):
            output += [f'GLASS_WRAP plan={p} frame={p} draw={draw} sequence={p*2+draw+1} valid=1 result=00000000 values='+','.join(map(str,ref.expected_wrap(p,depth)))]
            if s['transport']:
                output += [f'GLASS_TRANSPORT plan={p} frame={p} draw={draw} combined={int(material and draw)} alpha=alpha motion=motion depth={depth_hash} perspective=0.125 pixels={2048 if draw else 0} max_error=0 rgb_range={.5 if draw else 0}']
                for x in (16,32,48):
                    for y in (16,32,48):
                        rgb=[.125,.25,.5];rgb=[v**(1/2.2) for v in rgb] if material and draw else rgb
                        output += [f'GLASS_SAMPLE plan={p} frame={p} draw={draw} x={x} y={y} rgba='+','.join(map(str,rgb+[.625*128/255]))]
        image='combined' if s['combined'] else 'ordinary'
        output += [f'GLASS_LIVE plan={p} frame={p} pair={s["pair"]} step={s["step"]} vs={vs} ps={ps} combined={int(s["combined"])} refusal={s["refusal"]} matched={int(s["matched"])} rgba=.25,.5,1,{1 if p==18 else .625*128/255} alpha_hash=alpha image_hash={image}',f'MOTION_HASH frame={p} motion=motion depth={depth_hash}']
        routed=2*s['routed']
        trace += [f'motion_output_frame frame={p} routed={routed} depth_routed={routed*depth} matched={2*s["matched"]} gate4={2*(p in ref.GATED)} gate3=0 apply_failures=0 restore_failures=0 taa_resolved=0 rt_mode={mode}']
        if material: trace += [f'linear_material_frame frame={p} routed={(1 if s["transport"] else 2)*s["combined"]} refused={1 if s["transport"] else 2 if p in ref.SAMPLER_REFUSALS else 0} bind_failures=0 bump_routed=0']
    return '\n'.join(output),trace


class GlassLiveReportTests(unittest.TestCase):
    def test_scoped_matrix_and_exact_twins(self):
        cases={}
        for depth in (0,1):
            for mode in ('perdraw','lazy'):
                for material in (0,1):
                    output,trace=report(material,depth,mode)
                    cases[f'depth{depth}-{mode}-material{material}']=ref.validate_case(output,trace,material,depth,mode)
        ref.compare_cases(cases)
        self.assertEqual(sum(c['frames'] for c in cases.values()),216)
        self.assertEqual(len(ref.PROGRAM_NAMES),9)
        self.assertEqual(len(set(ref.PAIRS)),6)
        for key in ('alpha_hash','temporal_hashes','image_hash'):
            broken=copy.deepcopy(cases); broken['depth1-lazy-material1']['samples']['12'][key]='corrupt'
            with self.subTest(key=key),self.assertRaises(AssertionError):ref.compare_cases(broken)

    def test_missing_duplicate_or_failed_evidence_rejected(self):
        output,trace=report()
        for prefix in ('GLASS_LIVE ','GLASS_WRAP ','GLASS_TRANSPORT ','GLASS_SAMPLE ','MOTION_HASH ','RESULT PASS '):
            line=next(line for line in output.splitlines() if line.startswith(prefix))
            for changed in (output.replace(line+'\n','',1),output+'\n'+line):
                with self.subTest(prefix=prefix),self.assertRaises(AssertionError):ref.validate_case(changed,trace,True,True,'perdraw')
        for prefix in ('motion_output_frame ','linear_material_frame ','linear_material_variant ','motion_output_release '):
            index=next(i for i,line in enumerate(trace) if line.startswith(prefix))
            for changed in (trace[:index]+trace[index+1:],trace+[trace[index]]):
                with self.subTest(prefix=prefix),self.assertRaises(AssertionError):ref.validate_case(output,changed,True,True,'perdraw')
        with self.assertRaises(AssertionError):ref.validate_case(output+'\nRESULT FAIL injected',trace,True,True,'perdraw')

    def test_physical_wrap_mask_gate_and_rgb_corruption_rejected(self):
        output,trace=report()
        for old,new in [('values=13,11,6,0,0,0','values=13,11,6,0,15,0'),
                        ('max_error=0','max_error=1.001'),('rgb_range=0.5','rgb_range=0'),
                        ('refusal=5','refusal=0'),('matched=0','matched=1'),
                        ('alpha=alpha','alpha=changed')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):ref.validate_case(output.replace(old,new,1),trace,True,True,'perdraw')
        samples=[i for i,line in enumerate(output.splitlines()) if line.startswith('GLASS_SAMPLE ') and 'draw=1 ' in line]
        for replacement in ('nan,0,0,0.3','0,0,0,0.3137254901960784','0.3886,0.5325,0.7297,0.1'):
            rows=output.splitlines();rows[samples[0]]=rows[samples[0]].split('rgba=')[0]+'rgba='+replacement
            with self.assertRaises(AssertionError):ref.validate_case('\n'.join(rows),trace,True,True,'perdraw')
        for old,new in [('routed=0','routed=2'),('bind_failures=0','bind_failures=1'),('create=00000000','create=80004005')]:
            changed=[line.replace(old,new,1) for line in trace]
            with self.subTest(old=old),self.assertRaises(AssertionError):ref.validate_case(output,changed,True,True,'perdraw')

    def test_consume_only_glass_runner_contract(self):
        root=Path(__file__).resolve().parents[2]
        source=(root/'verification/probe/run_linear_material_live.py').read_text()
        self.assertIn("if args.mode=='glass': env['X3M_MATERIAL_EMISSIVE_GAIN']='1'",source)
        self.assertIn("'glass':'materialglass'",source)
        self.assertIn("glass_live.PROGRAM_NAMES if args.mode=='glass'",source)
        self.assertNotIn('cmake',source)
        self.assertNotIn('build_motion_output',source)
        self.assertIn('prebuilt inputs changed during qualification',source)


if __name__=='__main__': unittest.main()
