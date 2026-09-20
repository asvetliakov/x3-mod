import unittest
import tempfile
import json
from pathlib import Path
import numpy as np
import fog_spatial_timing_prepare as prep
import fog_spatial_timing_run as runner
import fog_spatial_timing_build as build

class PreparationTests(unittest.TestCase):
    def test_class_mismatch_requires_full24_repair(self):
        depth=np.zeros((2,2,4),np.float32);depth[...,0]=-1;depth[...,2]=1000;depth[0,0,0]=.5
        self.assertEqual(prep.repair_count(depth),3)
        depth[...,0]=.5;self.assertEqual(prep.repair_count(depth),0)
    def test_nonfinite_or_nonpositive_geometry_not_perf_input(self):
        for invalid in (0,-1,np.nan,np.inf):
            d=np.zeros((2,2,4),np.float32);d[...,0]=.5;d[...,2]=invalid
            with self.assertRaises(ValueError):prep.repair_count(d)
    def test_case_routing_and_input_mutation_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);physical=root/'physical';physical.write_text('original')
            record=dict(checkpoint='actual-production-whole-transaction-performance',borrowed_scene_open=True,cases=[dict(name='view',family='bluewell',directory=str(root),expected=dict(st=None,composite=None))],inputs={str(physical):prep.digest(physical)})
            (root/'cases.txt').write_text(prep.case_text(record));record['cases_sha256']=prep.digest(root/'cases.txt');(root/'manifest.json').write_text(json.dumps(record))
            prep.verify_prepared(root)
            physical.write_text('changed')
            with self.assertRaisesRegex(ValueError,'dependency'):prep.verify_prepared(root)
            physical.write_text('original');(root/'cases.txt').write_text('redirected')
            with self.assertRaisesRegex(ValueError,'routing'):prep.verify_prepared(root)
    def test_build_source_mutation_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)/'source.cpp';p.write_text('original');before=build.snapshot([p]);build.unchanged(before);p.write_text('changed')
            with self.assertRaises(ValueError):build.unchanged(before)
    def test_build_abi_flags_are_bound(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);exe=root/'fog_spatial_timing.exe';exe.write_bytes(b'frozen-executable')
            record=dict(kind='actual-production-timing',command=['i686-w64-mingw32-g++',*build.FLAGS],executable_sha256=prep.digest(exe),inputs={})
            path=root/'build.json';path.write_text(json.dumps(record));runner.verify_build(path)
            record['command'].remove('-mincoming-stack-boundary=2');path.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError,'flags'):runner.verify_build(path)
    def test_execution_cannot_relabel_executable_or_inputs(self):
        binding=dict(executable_sha256='exe',build_sha256='build',manifest_sha256='inputs',cases_sha256='routes')
        runner.verify_execution(binding,binding)
        for key in binding:
            altered=dict(binding);altered[key]='different'
            with self.assertRaises(ValueError):runner.verify_execution(altered,binding)

class TimingTests(unittest.TestCase):
    def setup_record(self,submit=100,fenced1280=900,fenced1920=1500):
        manifest=dict(profiles=[dict(name='1280x768',width=1280,height=768),dict(name='1920x1080',width=1920,height=1080)],cases=[])
        lines=['TIMING_CLOCK frequency=1000000 timestamps=not_collected_no_active_query_contract'];clock=1000
        for profile in manifest['profiles']:
            w,h=profile['width'],profile['height'];profile_name=profile['name']
            for family in ('bluewell','foggreenoutlands'):lines.append(f'TIMING_FIELD width={w} height={h} family={family} field_ticks=70 target_ticks=10 cpu_atlas_bytes=17846400')
            lines.append(f'TIMING_PREP width={w} height={h} ticks=100 families=2 atlas_bytes=35692800 input_bytes={4*w*h*32+2*w*h+128} pass_target_bytes={2*(8*w*h+8*((w+1)//2)*((h+1)//2))} witness_bytes={4*8*((w+1)//2)*((h+1)//2)} streams=16 counted_calls_exclude=resource_validation_and_COM_Releases')
            for i in range(4):
                name=f'{profile_name}-{i}';manifest['cases'].append(dict(name=name,profile=profile_name,width=w,height=h,family='bluewell' if i<2 else 'foggreenoutlands'))
                lines.append(f'CHECK {name}_baseline_state PASS')
                if w==1280:lines.append(f'CHECK {name}_accepted_bytes PASS')
            for i in range(80):
                elapsed=fenced1280 if w==1280 else fenced1920
                lines.append(f'TIMING_SAMPLE width={w} height={h} phase={"warm" if i<16 else "measured"} index={i if i<16 else i-16} view={profile_name}-{i%4} start={clock} submitted={clock+submit} completed={clock+elapsed} hr=00000000 restore=00000000 fence=00000000 valid=1 counted_calls=254')
                clock+=elapsed+100
            for i in range(4):lines.append(f'CHECK {profile_name}-{i}_final_bytes PASS')
            for family in ('bluewell','foggreenoutlands'):lines.append(f'CHECK {w}_{family}_stable_owned PASS')
            lines.append(f'CHECK {w}_stable_caller_refs PASS')
            lines.append(f'CHECK {w}_all_samples_recorded PASS')
        lines.append('RESULT checkpoint=timing profiles=2 warm_per_profile=16 samples_per_profile=64 PASS')
        return '\n'.join(lines),manifest
    def test_balanced_samples_and_fixed_gates(self):
        text,manifest=self.setup_record();result=runner.analyze(text,manifest)
        self.assertTrue(result['passed']);self.assertEqual(len(result['samples']),160)
        self.assertEqual(result['profiles'][0]['cpu_submit_ms']['median'],.1)
        self.assertEqual(result['profiles'][1]['event_fenced_ms']['p95'],1.5)
        self.assertTrue(all(v['count']==16 for p in result['profiles'] for v in p['views'].values()))
    def test_failed_gates_are_results_not_capability_refusal(self):
        for args in (dict(submit=251),dict(fenced1280=1251),dict(fenced1920=2001)):
            text,manifest=self.setup_record(**args)
            with self.subTest(args=args):self.assertFalse(runner.analyze(text,manifest)['passed'])
    def test_p95_gate_cannot_hide_behind_median(self):
        text,manifest=self.setup_record()
        lines=text.splitlines()
        for index,line in enumerate(lines):
            if 'width=1920' in line and 'phase=measured' in line:
                fields=dict(item.split('=',1) for item in line.split()[1:])
                # Enough slow samples for p95, keeping clocks monotone by making
                # all later timestamps share an additional fixed shift.
                if int(fields['index'])>=59:
                    start=int(fields['start'])+10000*(int(fields['index'])-58)
                    for key,value in (('start',start),('submitted',start+100),('completed',start+2600)):
                        line=line.replace(f'{key}={fields[key]}',f'{key}={value}')
                    lines[index]=line
        result=runner.analyze('\n'.join(lines),manifest)
        self.assertEqual(result['profiles'][1]['event_fenced_ms']['median'],1.5)
        self.assertFalse(result['passed'])
    def test_missing_duplicate_unfenced_or_invalid_samples_fail(self):
        text,manifest=self.setup_record();sample=next(line for line in text.splitlines() if line.startswith('TIMING_SAMPLE '))
        broken=(text.replace(sample+'\n',''),text.replace(sample,sample+'\n'+sample),text.replace('fence=00000000','fence=00000001',1),text.replace('valid=1','valid=0',1),text.replace('submitted=1100','submitted=1000',1),text.replace('counted_calls=254','counted_calls=0',1))
        for value in broken:
            with self.subTest(value=value[:30]),self.assertRaises(ValueError):runner.analyze(value,manifest)
    def test_missing_correctness_and_memory_records_fail(self):
        text,manifest=self.setup_record()
        for broken in (text.replace('CHECK 1280x768-0_final_bytes PASS\n',''),text.replace('atlas_bytes=35692800','atlas_bytes=17846400',1),text.replace('TIMING_CLOCK frequency=1000000','TIMING_CLOCK frequency=0')):
            with self.assertRaises(ValueError):runner.analyze(broken,manifest)
    def test_distribution_rejects_nan_and_negative(self):
        for values in ([float('nan')],[-1],[]):
            with self.assertRaises(ValueError):runner.distribution(values)




class FieldPreparationTests(unittest.TestCase):
    def test_missing_or_invalid_field_preparation_fails(self):
        text,manifest=TimingTests().setup_record();line=next(x for x in text.splitlines() if x.startswith('TIMING_FIELD'))
        for changed in (text.replace(line+'\n',''),text.replace('field_ticks=70','field_ticks=0',1),text.replace('cpu_atlas_bytes=17846400','cpu_atlas_bytes=35692800',1)):
            with self.assertRaises(ValueError):runner.analyze(changed,manifest)


class PairedTimingTests(unittest.TestCase):
    def fixture(self,slow_stress=False):
        profiles=[dict(name=f'{w}x{h}',width=w,height=h,warm_pairs_per_view=4,measured_pairs_per_view=16) for w,h in ((1280,768),(1920,1080))]
        maps={frame:dict(metadata='/maps/'+frame,maps=[dict(frame=int(frame),source=i,size=64,bias=.001,rows=[[1,0,0,0],[0,1,0,0],[0,0,1,0]],path='/map') for i in (1,2,3)]) for frame in prep.FRAMES}
        manifest=dict(paired_shadows=True,profiles=profiles,cases=[],shadow_maps=maps)
        lines=['PAIR_CLOCK frequency=1000000 timestamps=not_collected_no_active_query_contract','PAIR_MAP_PREP submit_ticks=10 completed_ticks=20 sets=4 bytes=196608'];clock=1000
        for profile in profiles:
            w,h=profile['width'],profile['height'];half=8*((w+1)//2)*((h+1)//2)
            lines.extend(f'PAIR_FIELD width={w} family={f} field_ticks=50 target_ticks=10 cpu_atlas_bytes=17846400' for f in ('bluewell','foggreenoutlands'))
            lines.append(f'PAIR_PREP width={w} height={h} ticks=100 atlas_bytes=35692800 input_bytes={5*w*h*32+2*w*h+128} target_bytes={2*(8*w*h+half)} witness_bytes={10*(8*w*h+half)}')
            cases=[]
            for i,frame in enumerate([*prep.FRAMES,'26447']):
                c=dict(name=f'{w}-{frame}'+('-repair' if i==4 else ''),frame=frame,family=prep.FRAMES[frame],profile=profile['name'],width=w,height=h,repair_stress=i==4,shadow_metadata=maps[frame]['metadata'])
                manifest['cases'].append(c);cases.append(c)
            for r in range(20):
                for i,c in enumerate(cases):
                    for order in range(2):
                        arm=(r&1)^order;duration=(10000 if slow_stress and i==4 else (800 if w==1280 else 1400))+arm*100
                        lines.append(f'PAIR_SAMPLE width={w} height={h} phase={"warm" if r<4 else "measured"} index={r if r<4 else r-4} pair={r*5+i} order={order} arm={arm} view={c["name"]} stress={int(i==4)} start={clock} submitted={clock+100+arm*10} completed={clock+duration} hr=00000000 restore=00000000 fence=00000000 valid=1 maps={3 if arm else 0} calls=296')
                        clock+=duration+100
        lines.extend('CHECK '+name+' PASS' for name in sorted(runner.paired_checks(manifest)))
        lines.append('RESULT checkpoint=shadow_pairs profiles=2 common_transactions=320 repair_transactions=80 PASS')
        return '\n'.join(lines),manifest
    def test_balanced_same_view_pairs_and_signed_deltas(self):
        text,manifest=self.fixture();result=runner.analyze_pairs(text,manifest)
        self.assertTrue(result['passed']);self.assertEqual(len(result['samples']),400);self.assertEqual(len(result['checks']),63)
        for p in result['profiles']:
            self.assertEqual(p['common']['pairs'],64);self.assertEqual(p['repair_stress']['pairs'],16)
            self.assertAlmostEqual(p['common']['paired_delta_ms']['event_fenced']['median'],.1)
        self.assertAlmostEqual(runner.signed_distribution([-.2,-.1])['median'],-.15)
    def test_slow_repair_stress_never_masks_or_fails_common_gate(self):
        text,manifest=self.fixture(slow_stress=True);result=runner.analyze_pairs(text,manifest)
        self.assertTrue(result['passed']);self.assertGreater(result['profiles'][0]['repair_stress']['on']['event_fenced_ms']['median'],10)
        # Constant overhead shared by both arms must still fail absolute gates.
        changed=[];shift=0
        for line in text.splitlines():
            if line.startswith('PAIR_SAMPLE '):
                values=dict(v.split('=',1) for v in line.split()[1:])
                for key in ('start','submitted','completed'):
                    v=int(values[key])+shift+(1000 if key=='completed' else 0)
                    line=line.replace(key+'='+values[key],key+'='+str(v))
                shift+=1000
            changed.append(line)
        self.assertFalse(runner.analyze_pairs('\n'.join(changed),manifest)['passed'])
    def test_missing_duplicate_wrong_arms_or_maps_rejected(self):
        text,manifest=self.fixture();sample=next(l for l in text.splitlines() if l.startswith('PAIR_SAMPLE '))
        for bad in (text.replace(sample+'\n',''),text.replace(sample,sample+'\n'+sample),text.replace('order=0 arm=0','order=0 arm=1',1),text.replace('maps=3','maps=0',1),text.replace('fence=00000000','fence=00000001',1),text.replace('calls=296','calls=0',1)):
            with self.assertRaises(ValueError):runner.analyze_pairs(bad,manifest)
    def test_rejects_stale_maps_wrong_workload_and_missing_preparation(self):
        text,manifest=self.fixture();manifest['shadow_maps']['1974']['maps'][0]['frame']=1973
        with self.assertRaisesRegex(ValueError,'current-map'):runner.analyze_pairs(text,manifest)
        text,manifest=self.fixture();manifest['cases'][0]['shadow_metadata']='/stale'
        with self.assertRaises(ValueError):runner.analyze_pairs(text,manifest)
        text,manifest=self.fixture();line=next(l for l in text.splitlines() if l.startswith('PAIR_FIELD '))
        with self.assertRaises(ValueError):runner.analyze_pairs(text.replace(line+'\n',''),manifest)
    def test_repair_derivative_changes_only_declared_half_depth(self):
        original=np.ones((4,4,4),np.float32);original[...,2]=100
        derived=original.copy();derived[::2,::2,0]=.5;derived[::2,::2,2]=np.nan
        self.assertEqual(prep.verify_repair_depth(derived,original),12)
        derived[1,1,2]=101
        with self.assertRaises(ValueError):prep.verify_repair_depth(derived,original)
    def test_metadata_serialization_and_case_routing(self):
        _,manifest=self.fixture();entry=manifest['shadow_maps']['1974'];text=prep.shadow_text('1974',entry['maps'])
        self.assertEqual(len(text.splitlines()),7);self.assertEqual(text.splitlines()[0],'1974')
        for c in manifest['cases']:c.update(directory='/input',expected=dict(composite=None,st=None))
        self.assertEqual(len(prep.case_text(manifest).splitlines()),80)
        entry['maps'][1]['source']=0
        with self.assertRaisesRegex(ValueError,'current-map'):prep.validate_shadow_manifest(manifest)

if __name__=='__main__':unittest.main()
