from verification.analysis.retired_tests import load_tests  # retired feature: hidden from default discovery
import tempfile
import struct
import re
from pathlib import Path
import unittest
import run_linear_emission_live as r


def witness(enabled=True):
    lines=['RESET PASS'];trace=[]
    for i,draws in enumerate(r.DRAWS):
        prepared=draws if enabled and i!=5 else 0
        row=dict(frame=i,enabled=int(enabled),draws=draws,mask_valid=int(enabled and i not in (8,9)),prepared=prepared,
                 linear=draws if enabled and i not in (5,6,7,8,9) else 0,native=int(enabled and i in (6,7,9)),incomplete=int(enabled and i==8),
                 refused=(1+int(i in (5,8)) if enabled else 0),suppressed=0,exchanged=prepared,original_calls=draws,source_hr='8876086c' if enabled and i==8 else '00000000',fault=r.FAULTS.get(i,0))
        lines.append('EMISSION_LIVE '+' '.join(f'{k}={v}' for k,v in row.items()))
        if i>=r.LEGACY_FRAMES:
            pair=(i-r.LEGACY_FRAMES)//2;v,p=r.PAIR_INDICES[pair]
            lines.append(f'EMISSION_CORPUS frame={i} pair={pair} vs={r.VERTICES[v]} ps={r.PIXELS[p]} '
                         f'instance={int(3<=v<=6)} affine={int(p not in (2,4,5,6))} '
                         f'fade={int(p not in (3,4))} fog={int(p not in (3,4))} disappeared={i%2}')
        routed=1 if i==8 else 2;resolved=int(i!=8)
        trace += [f'motion_output_frame frame={i} draws={3+draws} routed={routed} depth_routed={routed} matched={1 if i in (8,9) else 2} jittered={routed} '
                  f'cut={int(i==9)} cut_missing={.5 if i==9 else 0} taa_attempted={resolved} taa_resolved={resolved} '
                  f'taa_skip={0 if resolved else 2} scene_end_source={"stretchrect" if resolved else "none"} taa_history={int(i not in (0,8,9,10))} '
                  f'mip_bias_sets={routed} mip_bias_restores={routed} mip_bias_draws={routed} mip_bias_stages=0001 mip_bias_failures=0 mip_bias_biased_now=0000',
                  f'hdr_frame frame={i} end={"present" if i==8 else "bloom_copy"} writebacks=1 tonemapped=1 writeback_source=shader tonemap=agx fallback=0 unwind=0']
        if resolved:trace.append(f'motion_output_taa_readback frame={i} result=00000000')
        if enabled:trace.append(f'linear_emission_frame frame={i} exports=0 quarantine=0 state_lost=0')
    lines.append('EMISSION_REJECTED frame=8 source_failed=1 original_b=1 b_routed=0 b_jittered=0 taa=0 history_seeded=0 copy_exact=1 native_hr=8876086c')
    lines += [f'EMISSION_CHECKS frames={r.FRAMES} submissions={sum(r.DRAWS)} numeric=100 max_fraction=.5 reset=1 ordered_overwrites=20',
              f'RESULT PASS checks=100 restorations=50 frames={r.FRAMES} taa_reference_frames={r.FRAMES-1} taa_skipped_frames=1 taa_frames={r.FRAMES-1} taa_history_frames={r.FRAMES-4}']
    trace += ['motion_output_release released=1 held=20']
    return '\n'.join(lines),'\n'.join(trace)


class EmissionLiveTests(unittest.TestCase):
    def test_on_off_and_once_counts(self):
        for enabled in (False,True):
            result=r.validate(*witness(enabled),enabled)
            self.assertEqual(result['frames'],52)
            self.assertEqual(result['submissions'],30)
            self.assertEqual(result['exact_temporal_frames'],51)

    def test_all_exact_pairs_and_native_layouts_required(self):
        text,trace=witness()
        for old,new in [('vs=5b7a3ccd9e7df00a','vs=32e75459998d0388'),
                        ('ps=47e15e20d63b0e93','ps=39f3b4d5b6a5aaed'),
                        ('instance=1','instance=0'),('fog=1','fog=0'),
                        ('disappeared=1','disappeared=0')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):
                r.validate(text.replace(old,new,1),trace,True)
        for frame in (12,13,50,51):
            missing='\n'.join(x for x in text.splitlines()
                              if not x.startswith(f'EMISSION_CORPUS frame={frame} '))
            with self.subTest(frame=frame),self.assertRaises(AssertionError):
                r.validate(missing,trace,True)

    def test_fixture_inventory_matches_production_and_preserves_native_models(self):
        source=(r.ROOT/'src/renderer/linear_emission.cpp').read_text()
        table=source.split('constexpr Pair pairs[] = {',1)[1].split('};',1)[0]
        self.assertEqual(tuple(re.findall(r'0x([0-9a-f]{16})ull,0x([0-9a-f]{16})ull',table)),r.PAIRS)
        fixture=(r.ROOT/'verification/probe/motion_output_emission_inc.h').read_text()
        for kind,values in (('vertex',r.VERTICES),('pixel',r.PIXELS)):
            table=fixture.split(f'{kind}_ids[] = {{',1)[1].split('};',1)[0]
            self.assertEqual(tuple(re.findall(r'0x([0-9a-f]{16})ull',table)),values)
        table=fixture.split('const EmissionPair pairs[] = {',1)[1].split('};',1)[0]
        self.assertEqual(tuple(tuple(map(int,pair)) for pair in re.findall(r'\{(\d+),(\d+)\}',table)),r.PAIR_INDICES)
        self.assertEqual((len(set(r.PAIRS)),len(r.VERTICES),len(r.PIXELS)),(20,8,10))
        self.assertIn('0xffff0201u : 0xffff0200u',fixture)
        self.assertIn('instance ? 11 : 13',fixture)
        self.assertIn('instance ? 10 : 12',fixture)
        self.assertIn('faded ? 10 : 4',fixture)
        self.assertIn('f.scope(nullptr)',fixture)

    def test_duplicate_original_or_exchange_missing_rejected(self):
        text,trace=witness()
        for old,new in [('original_calls=1','original_calls=2'),('exchanged=1','exchanged=0'),('prepared=1','prepared=0')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):r.validate(text.replace(old,new,1),trace,True)

    def test_native_counter_is_independent_of_preparation(self):
        for enabled in (False,True):
            text,trace=witness(enabled)
            # Feature-off and clean preparation refusal still execute native DIP.
            line=next(x for x in text.splitlines() if x.startswith('EMISSION_LIVE frame=5 '))
            self.assertIn('prepared=0',line)
            with self.assertRaises(AssertionError):r.validate(text.replace(line,line.replace('original_calls=1','original_calls=0')),trace,enabled)

    def test_failed_source_cannot_be_reported_complete(self):
        text,trace=witness()
        for old,new in [('incomplete=1','incomplete=0'),('source_hr=8876086c','source_hr=00000000')]:
            with self.assertRaises(AssertionError):r.validate(text.replace(old,new),trace,True)

    def test_missing_coverage_and_recovery_policy(self):
        text,trace=witness()
        with self.assertRaises(AssertionError):r.validate(text.replace('mask_valid=0 prepared=1','mask_valid=1 prepared=1'),trace,True)

    def test_motion_temporal_reset_and_release_remain_mandatory(self):
        text,trace=witness()
        for old,new in [('routed=2','routed=1'),('taa_resolved=1','taa_resolved=0'),('released=1','released=0')]:
            with self.assertRaises(AssertionError):r.validate(text,trace.replace(old,new,1),True)
        with self.assertRaises(AssertionError):r.validate(text.replace('RESET PASS',''),trace,True)

    def test_exact_alpha_inputs_avoid_nonrepresentable_second_sum(self):
        half=lambda x:struct.unpack('<e',struct.pack('<e',x))[0]
        source=(r.ROOT/'verification/probe/motion_output_emission_inc.h').read_text()
        block=re.search(r'const float texels\[2\]\[4\] = \{(.*?)\};',source,re.S).group(1)
        values=[float(x) for x in re.findall(r'(\.\d+)f',block)]
        self.assertEqual((values[3],values[7]),(.125,.25))
        base=.39013671875  # Actual R2 retained FP16 material alpha.
        first=base+values[3]
        self.assertEqual(half(first),first)
        self.assertNotEqual(half(first+.5),first+.5)  # Rejected old input.
        for before in (base,first):
            self.assertEqual(half(before+values[7]),before+values[7])

    def test_real_mip_bias_and_no_leak_required(self):
        text,trace=witness()
        for old,new in [('mip_bias_sets=2','mip_bias_sets=1'),
                        ('mip_bias_restores=2','mip_bias_restores=1'),
                        ('mip_bias_draws=2','mip_bias_draws=1'),
                        ('mip_bias_stages=0001','mip_bias_stages=0003'),
                        ('mip_bias_failures=0','mip_bias_failures=1'),
                        ('mip_bias_biased_now=0000','mip_bias_biased_now=0001')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):
                r.validate(text,trace.replace(old,new,1),True)

    def test_successful_resolve_readbacks_include_reset_and_first_frame(self):
        text,trace=witness()
        for frame in (0,10,11,12,13,50,51):
            missing='\n'.join(x for x in trace.splitlines()
                              if not x.startswith(f'motion_output_taa_readback frame={frame} '))
            with self.subTest(frame=frame),self.assertRaises(AssertionError):
                r.validate(text,missing,True)
        source=(r.ROOT/'verification/probe/motion_output_fixture.cpp').read_text()
        # 155ac54 (sun-share runtime lane) added the sun lane as a second
        # reason to force the readback; the emission arm is unchanged.
        self.assertIn('config.force_taa_readback = sunlane || (emissions && !emission_bench);',source)

    def test_completion_timing_scope_and_sample_checks(self):
        text='\n'.join(f'EMISSION_TIMING sample={i} enabled=1 width=1920 height=1080 completed_ms={1+i*.1}' for i in range(8))
        text+='\nEMISSION_BENCH frames=12 samples=8 warmups=4 submissions=12 coverage=0.25\nRESULT PASS frames=12'
        self.assertAlmostEqual(r.validate_timing(text,True)['median_ms'],1.35)
        for old,new in [('sample=0','sample=1'),('completed_ms=1.0','completed_ms=nan'),('submissions=12','submissions=24')]:
            with self.assertRaises(AssertionError):r.validate_timing(text.replace(old,new,1),True)

    def test_temporal_raw_requires_exact_same_inputs_result(self):
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder);capture=work/'x3-modern-captures';capture.mkdir()
            for i in range(r.FRAMES):
                if i in r.TAA_FRAMES:
                    (capture/f'taa_1_{i}.rgba16f').write_bytes(bytes(64*64*8))
                    (work/f'reference_taa_{i}.rgba16f').write_bytes(bytes(64*64*8))
                (work/f'emission_mask_{i}.rgba32f').write_bytes(bytes(64*64*16))
            (work/'emission_color_8.rgba32f').write_bytes(bytes(64*64*16))
            (work/'presented_8.bgra8').write_bytes(bytes(64*64*4))
            self.assertEqual(r.validate_pixels(work,False)['covered_pixels'],[0]*r.FRAMES)
            with self.assertRaises(AssertionError):r.validate_pixels(work,True)
            mask=[0.]*(64*64*4)
            for y in range(16,48):
                for x in range(8,40):
                    for k in range(3):mask[(y*64+x)*4+k]=1.
            encoded=struct.pack('<16384f',*mask)
            for i in range(r.LEGACY_FRAMES,r.FRAMES,2):
                (work/f'emission_mask_{i}.rgba32f').write_bytes(encoded)
            self.assertEqual(r.validate_pixels(work,True)['covered_pixels'][r.LEGACY_FRAMES:],[1024,0]*20)
            (work/'emission_mask_51.rgba32f').write_bytes(encoded)
            with self.assertRaises(AssertionError):r.validate_pixels(work,True)
            (work/'emission_mask_51.rgba32f').write_bytes(bytes(64*64*16))
            mask[(16*64+8)*4+1]=0.
            (work/'emission_mask_50.rgba32f').write_bytes(struct.pack('<16384f',*mask))
            with self.assertRaises(AssertionError):r.validate_pixels(work,True)
            (work/'emission_mask_50.rgba32f').write_bytes(encoded)
            (capture/'taa_1_8.rgba16f').write_bytes(bytes(64*64*8))
            with self.assertRaises(AssertionError):r.validate_pixels(work)
            (capture/'taa_1_8.rgba16f').unlink()
            bad=bytearray(64*64*4);bad[0]=3
            (work/'presented_8.bgra8').write_bytes(bad)
            with self.assertRaises(AssertionError):r.validate_pixels(work)
            (work/'presented_8.bgra8').write_bytes(bytes(64*64*4))
            data=bytearray(64*64*8);data[0]=1;(capture/'taa_1_1.rgba16f').write_bytes(data)
            with self.assertRaises(AssertionError):r.validate_pixels(work)

    def test_failed_frame_and_recovery_are_specific_not_relaxed(self):
        text,trace=witness()
        for frame,old,new in [(8,'routed=1','routed=2'),(8,'jittered=1','jittered=2'),
                              (8,'taa_skip=2','taa_skip=0'),(8,'taa_history=0','taa_history=1'),
                              (9,'matched=1','matched=2'),(9,'cut=1','cut=0'),(9,'cut_missing=0.5','cut_missing=0')]:
            line=next(x for x in trace.splitlines() if x.startswith(f'motion_output_frame frame={frame} '))
            with self.subTest(frame=frame,old=old),self.assertRaises(AssertionError):
                r.validate(text,trace.replace(line,line.replace(old,new)),True)
        for old,new in [('quarantine=0','quarantine=1'),('exports=0','exports=1'),('state_lost=0','state_lost=1'),('end=present','end=bloom_copy'),('writebacks=1','writebacks=2'),('fallback=0','fallback=1'),('unwind=0','unwind=1')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):
                r.validate(text,trace.replace(old,new,1),True)
        with self.assertRaises(AssertionError):r.validate(text.replace('copy_exact=1','copy_exact=0'),trace,True)
        with self.assertRaises(AssertionError):r.validate(text.replace('native_hr=8876086c','native_hr=80004005'),trace,True)
        for old,new in [('refused=2','refused=1'),('suppressed=0','suppressed=1')]:
            with self.assertRaises(AssertionError):r.validate(text.replace(old,new,1),trace,True)

    def test_numeric_and_frame_completion_bounds(self):
        text,trace=witness()
        for old,new in [('max_fraction=.5','max_fraction=1.001'),('max_fraction=.5','max_fraction=nan'),('frames=52 submissions','frames=51 submissions'),('RESULT PASS','RESULT FAIL')]:
            with self.assertRaises(AssertionError):r.validate(text.replace(old,new,1),trace,True)


if __name__=='__main__':unittest.main()
