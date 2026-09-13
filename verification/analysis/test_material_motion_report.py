"""Offline acceptance controls for the same-draw native report."""
from pathlib import Path
import importlib.util
import unittest
import struct
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('material_motion_report',ROOT/'verification/probe/run_material_motion.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class ReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text=(ROOT/'verification/results/material-motion.txt').read_text()
        # The retained capture predates class D. Its original A/B/C inventory
        # remains unchanged; do not invent dedicated-mode skips in old evidence.
        cls.table=[row for row in module.table_rows() if row[4] in ('A','B','C')]
        assert len(cls.table)==169
    def reject(self,text):
        with self.assertRaises((AssertionError,KeyError,IndexError)):module.validate_report(text, expected_table=self.table)
    def test_current(self):self.assertEqual(module.validate_report(self.text, expected_table=self.table)['configurations'],82)
    def test_zero_checks(self):self.reject('RESULT PASS checks=0 numerical=0 color_components=0 depth_cases=0 configurations=0 devices=2\n')
    def test_missing_check(self):self.reject(self.text.replace('CHECK exact local shader pair transformed PASS\n','',1))
    def test_duplicate_terminal(self):self.reject(self.text+self.text.splitlines()[-1]+'\n')
    def test_trailing_output(self):self.reject(self.text+'extra\n')
    def test_wrong_device(self):self.reject(self.text.replace('DEVICE pure=1','DEVICE pure=0'))
    def test_wrong_module(self):self.reject(self.text.replace('path=C:\\windows\\system32\\d3d9.dll','path=C:\\X3\\d3d9.dll'))
    def test_missing_reset(self):self.reject(self.text.replace('RESET PASS\n','',1))
    def test_wrong_motion_inventory(self):self.reject(self.text.replace('SAMPLE config=1 x=8 y=8 channel=0','SAMPLE config=1 x=8 y=8 channel=1',1))
    def test_missing_timing(self):self.reject('\n'.join(s for s in self.text.splitlines() if not s.startswith('TIMING width=1280 height=768 format=21 iteration=0 '))+'\n')
    def test_wrong_light_control(self):self.reject(self.text.replace('native point-light count changes original material','unrelated control',1))
    def test_wrong_scene_pairs(self):self.reject(self.text.replace('scene_pairs=1','scene_pairs=2',1))
    def test_wrong_draw_count(self):self.reject(self.text.replace('scene_pairs=1 draws=1','scene_pairs=1 draws=2',1))
    def test_wrong_mixed_cap(self):self.reject(self.text.replace('DEVICE pure=0 mixed=1','DEVICE pure=0 mixed=0'))

class DamageReportTests(unittest.TestCase):
    """Small synthetic transcript, independent of GPU availability/current archives."""
    @staticmethod
    def transcript(mixed=True):
        lines=[f'DAMAGE_BEGIN mixed={int(mixed)}', r'DAMAGE_MODULE name=d3d9.dll path=C:\windows\system32\d3d9.dll', f'DAMAGE_CAPS mrt=4 misc={0x40000 if mixed else 0:08x} vs=fffe0300 ps=ffff0300 max_vs_const=256 vs_slots=512 ps_slots=512'];cid=0
        for generation in range(2):
            for ps in module.DAMAGE_PS:
                for depth in range(2):
                    for poison in range(2):
                        for winding in range(2 if ps==module.DAMAGE_PS[0] else 1):
                            for fmt in ((116,21) if mixed else (116,)):
                                for label in ('damage native b0 isolated sensitivity', 'damage native b1 isolated sensitivity', 'damage native alpha sensitivity', 'damage native occlusion sensitivity', 'damage native detail sensitivity'):
                                    lines.append('CHECK '+label+' PASS')
                                for b in range(4):
                                    lines.append(f'DAMAGE_CASE ps={ps} generation={generation} depth={depth} poison={poison} winding={winding} format={fmt} booleans={b}')
                                    lines.append(f'DAMAGE_WITNESS booleans={b} winding={winding} below=11 equal=11 above=10 face_sign={1 if winding==0 else -1} bad=0')
                                    if cid==0:
                                        for x in range(32):
                                            pre=.5-.5*(x%3);actual=[max(pre,0.),pre,float(x%3==2),1.];sampled=[.25+.25*(x%3),(x+.5)/32,16.5/32,1.]
                                            fmt_values=lambda values: ','.join(format(v,'.9g') for v in values)
                                            fmt_bits=lambda values: ','.join(struct.pack('>f',v).hex() for v in values)
                                            lines.append(f'DAMAGE_WITNESS_PIXEL x={x} y=16 expected={fmt_values(actual[:3])} expected_uv={fmt_values(sampled[1:3])} actual={fmt_values(actual)} bits={fmt_bits(actual)} sampled_red_uv_face={fmt_values(sampled)} sampled_bits={fmt_bits(sampled)}')
                                    lines.append('CHECK adjacent damage IFC false threshold true native witness PASS')
                                    for variant in range(3):
                                        mode=int(variant>0)
                                        cid+=1
                                        lines.append(f'CONFIG id={cid} width=32 height=32 format={fmt} packed={b&1} perspective={mode} lights=0 valid={int(variant<2)} translation={.125*mode:.6f} jitter={.25*mode:.6f},{-.375*mode if mode else 0:.6f} booleans={b} depth_slope={.05*mode:.6f}')
                                        for label in ('same_draw','bilateral_depth_equal','bilateral_depth_equal'):
                                            lines.append(f'COLOR {label} width=32 height=32 format={fmt} components=4096 covered=1000 mismatches=0 maximum=0')
                                        for y in (8,16,24):
                                            for x in (8,16,24):
                                                for k in range(4):lines.append(f'SAMPLE config={cid} x={x} y={y} channel={k} actual=0 expected=0 error=0 pixel_error=0 PASS')
                                                if depth:lines.append(f'DEPTH_SAMPLE config={cid} x={x} y={y} actual=0.5 expected=0.5 error=0 PASS')
                                        lines.append(f'REFERENCE config={cid} components=4096 mismatches=0 max=0')
                                        if depth:
                                            lines.append(f'DEPTH_COVERAGE config={cid} covered=1000 bad=0')
                                            lines.append(f'DEPTH_REFERENCE config={cid} compared=1000 mismatches=0 max=0')
                                        else:lines.append('CHECK motion-only depth target remains clear PASS')
                                        lines.append('CHECK changed depth rejects all tested fragments PASS')
            if generation==0:lines.append('DAMAGE_RESET PASS')
        for ps in module.DAMAGE_PS:
            lines.append(f'DAMAGE_TIMING ps={ps}')
            for i in range(18):
                mode=2-i%3 if (i//3)%2 else i%3
                lines.append(f'TIMING width=1280 height=768 format=116 iteration={i} mode={mode} completed_ms=0.1 timed_readback=0 gpu_timestamp=0 scene_pairs=1 draws={2 if mode==2 else 1}')
        lines.append(f'DAMAGE_RESULT PASS checks={sum(l.startswith("CHECK ") for l in lines)} numerical={cid*36} color_components={cid*4096*3} depth_cases={cid*2} configurations={cid}')
        return '\n'.join(lines)+'\n'
    @classmethod
    def setUpClass(cls):cls.text=cls.transcript()
    def reject(self,text):
        with self.assertRaises((AssertionError,KeyError,IndexError,StopIteration)):module.validate_damage_report(text)
    def test_complete_inventory(self):self.assertEqual(module.validate_damage_report(self.text)['configurations'],576)
    def test_no_mixed_inventory(self):self.assertEqual(module.validate_damage_report(self.transcript(False))['configurations'],288)
    def test_missing_poison(self):self.reject(self.text.replace('poison=1','poison=0'))
    def test_missing_threshold(self):self.reject(self.text.replace('equal=11','equal=0',1))
    def test_wrong_face(self):self.reject(self.text.replace('face_sign=-1','face_sign=1'))
    def test_wrong_boolean(self):self.reject(self.text.replace('DAMAGE_WITNESS booleans=1','DAMAGE_WITNESS booleans=0',1))
    def test_bad_native_branch(self):self.reject(self.text.replace('face_sign=1 bad=0','face_sign=1 bad=1',1))
    def test_wrong_native_runtime(self):self.reject(self.text.replace(r'C:\windows\system32\d3d9.dll',r'C:\X3\d3d9.dll'))
    def test_wrong_caps(self):self.reject(self.text.replace('mrt=4','mrt=2'))
    def test_moving_valid_history_missing(self):self.reject(self.text.replace('perspective=1 lights=0 valid=1','perspective=1 lights=0 valid=0',1))
    def test_missing_first_trace(self):self.reject('\n'.join(l for l in self.text.splitlines() if not l.startswith('DAMAGE_WITNESS_PIXEL x=0 '))+'\n')
    def test_wrong_trace_float_bits(self):self.reject(self.text.replace('sampled_bits=3e800000','sampled_bits=3f000000',1))
    def test_wrong_trace_pixel(self):self.reject(self.text.replace('DAMAGE_WITNESS_PIXEL x=0 y=16','DAMAGE_WITNESS_PIXEL x=1 y=16',1))
    def test_nonfinite_trace(self):self.reject(self.text.replace('actual=0.5,0.5,0,1','actual=nan,0.5,0,1',1))
    def test_trace_cannot_mask_bad_witness(self):self.reject(self.text.replace('face_sign=1 bad=0','face_sign=1 bad=1',1))
    def test_trace_allows_safe_uv_rounding(self):
        uv=.015625+2**-20
        changed=self.text.replace('sampled_red_uv_face=0.25,0.015625,0.515625,1 sampled_bits=3e800000,3c800000',f'sampled_red_uv_face=0.25,{uv:.9g},0.515625,1 sampled_bits=3e800000,{struct.pack(">f",uv).hex()}',1)
        self.assertEqual(module.validate_damage_report(changed)['configurations'],576)
    def test_trace_face_magnitude_uses_sign(self):
        changed=self.text.replace('sampled_red_uv_face=0.25,0.015625,0.515625,1 sampled_bits=3e800000,3c800000,3f040000,3f800000','sampled_red_uv_face=0.25,0.015625,0.515625,2 sampled_bits=3e800000,3c800000,3f040000,40000000',1)
        self.assertEqual(module.validate_damage_report(changed)['configurations'],576)
    def test_wrong_trace_sampling_phase(self):self.reject(self.text.replace('sampled_red_uv_face=0.25,0.015625,0.515625,1 sampled_bits=3e800000,3c800000','sampled_red_uv_face=0.25,0,0.515625,1 sampled_bits=3e800000,00000000',1))
    def test_missing_b0_sensitivity(self):self.reject(self.text.replace('CHECK damage native b0 isolated sensitivity PASS\n','',1))
    def test_missing_b1_sensitivity(self):self.reject(self.text.replace('CHECK damage native b1 isolated sensitivity PASS\n','',1))
    def test_one_boolean_control_cannot_replace_other(self):self.reject(self.text.replace('damage native b1 isolated sensitivity','damage native b0 isolated sensitivity'))
    def test_aggregate_boolean_control_rejected(self):self.reject(self.text.replace('damage native b0 isolated sensitivity','boolean branches change original material').replace('damage native b1 isolated sensitivity','boolean branches change original material'))
    def test_missing_sensitivity(self):self.reject(self.text.replace('CHECK damage native detail sensitivity PASS\n','',1))
    def test_missing_reset(self):self.reject(self.text.replace('DAMAGE_RESET PASS\n',''))
    def test_missing_motion_only_control(self):self.reject(self.text.replace('CHECK motion-only depth target remains clear PASS\n','',1))
    def test_wrong_motion_lane(self):self.reject(self.text.replace('SAMPLE config=1 x=8 y=8 channel=0','SAMPLE config=1 x=8 y=8 channel=1',1))
    def test_nonfinite_reference(self):self.reject(self.text.replace('components=4096 mismatches=0 max=0','components=4096 mismatches=0 max=nan',1))
    def test_missing_depth_sample(self):self.reject('\n'.join(l for l in self.text.splitlines() if not l.startswith('DEPTH_SAMPLE config=97 x=8 y=8 '))+'\n')
    def test_trailing(self):self.reject(self.text+'extra\n')
    def test_class_d_dedicated(self):
        row=(module.DAMAGE_VS,768,module.DAMAGE_PS[0],1746,'D')
        text=['DEVICE pure=0 mixed=1', f'ROW index=0 vs={row[0]} ps={row[2]} class=D status=SKIP reason=dedicated_damage_fixture']
        result,_=module.validate_rows(text,True,1,[row],[{'vs':{},'ps':{}}])
        self.assertEqual(result[0]['reason'],'dedicated_damage_fixture')

if __name__=='__main__':unittest.main()
