"""Component report gates, reusing the qualified actual-source numeric oracle."""
import struct
import copy
import re
import unittest
import run_linear_emission as r
from verification.analysis.test_linear_emission_coverage_report import report as coverage_report


def report(cases):
    text,data=coverage_report(cases)
    lines=[line for line in text.splitlines() if not line.startswith(('COVERAGE_CASE','COVERAGE_TIMING','COVERAGE_RESULT'))]
    lines += [f"PASS_CASE id={c['id']} channels=4096 draws={len(c['ops'])} allocations=0" for c in cases]
    lines += [f"PASS_CHECKS channels={4096*len(cases)} draws={sum(len(c['ops']) for c in cases)}",
              'PASS_FAULTS checks=16 source_replays=0','PASS_CAPS twins=2 forbidden_calls=0',
              'PASS_RESET passed=1 retained_programs=4 recreated_targets=4 frame_clear=1 transaction=1']
    for w,h in ((1280,768),(1920,1080)):
        for pair in range(8):
            for order in range(2):
                variant=1-order if pair%2 else order
                lines.append(f'PASS_TIMING width={w} height={h} variant={variant} pair={pair} order={order} completed_ms={1+pair*.125+variant*.0625}')
    lines.append(f'PASS_RESULT pass cases={len(cases)} shaders=528')
    return '\n'.join(lines)+'\n',data


class PassReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases=r.pass_cases();cls.text,cls.data=report(cls.cases)

    def validate(self,text=None,data=None):
        return r.validate_coverage_report(text if text is not None else self.text,data if data is not None else self.data,self.cases,True)

    def test_admitted_cases_keep_original_profiles_and_single_draws(self):
        self.assertEqual(len(self.cases),271)
        self.assertEqual(sum(len(c['ops']) for c in self.cases),371)
        self.assertEqual({c['actual_profile'] for c in self.cases},set(range(20)))
        self.assertTrue(all(c['flags']&2 and not c['mask'] for c in self.cases))
        result=self.validate()
        self.assertEqual(result['component_comparison_channels'],1110016)
        self.assertEqual(result['shader_creations'],528)
        self.assertEqual(result['steady_allocations'],0)
        self.assertTrue(result['native_reset_passed'])

    def test_component_proof_is_mandatory(self):
        for old,new in [('channels=4096','channels=4095'),('allocations=0','allocations=1'),('checks=16','checks=15'),
                        ('source_replays=0','source_replays=1'),('forbidden_calls=0','forbidden_calls=1'),
                        ('retained_programs=4','retained_programs=0'),('transaction=1','transaction=0'),('shaders=528','shaders=527')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):self.validate(self.text.replace(old,new,1))

    def test_color_alpha_depth_energy_mask_checks_remain(self):
        for offset in (4,4+12,4+256*4*4,4+256*9*4,4+256*13*4):
            data=bytearray(self.data);value=struct.unpack_from('<f',data,offset)[0];struct.pack_into('<f',data,offset,value+1)
            with self.subTest(offset=offset),self.assertRaises(AssertionError):self.validate(data=data)

    def test_caps_and_clean_exit_are_required(self):
        for old,new in [('slots=4','slots=2'),('postblend=1','postblend=0'),('PASS_RESULT pass','PASS_RESULT failed')]:
            with self.subTest(old=old),self.assertRaises(AssertionError):self.validate(self.text.replace(old,new))

    def test_gpu_feedback_category_does_not_predict_threshold_side(self):
        # Retained X3 witness: first composition stores154.5, while CPU/half
        # stores154.625. Second source misses pixel0; E0 retains actual154.5.
        c=self.cases[1]
        self.assertTrue(r.emitting_feedback(c))
        text=re.sub(r'(MRT_CASE id=1 .*? capzero=)2',r'\g<1>0',self.text)
        result=self.validate(text)
        self.assertEqual(result['invariants']['capzero'],self.validate()['invariants']['capzero']-2)
        self.assertEqual(result['high_code_identity']['feedback_categories'][0],dict(id=1,capzero=0))
        self.assertGreater(result['high_code_identity']['stable_seed_channels'],0)
        # Classification is descriptive, not an observed {0,2} special case.
        self.validate(re.sub(r'(MRT_CASE id=1 .*? capzero=)2',r'\g<1>1',self.text))
        impossible=r.mrt_expected(c)[2]['zero']+1
        text=re.sub(r'(MRT_CASE id=1 .*? capzero=)2',lambda m:m[1]+str(impossible),self.text)
        with self.assertRaises(AssertionError):self.validate(text)

    def test_static_seed_and_zero_energy_categories_remain_exact(self):
        self.assertFalse(r.emitting_feedback(self.cases[0]))
        c=copy.deepcopy(self.cases[1]);c['ops']=c['ops'][:1]
        self.assertFalse(r.emitting_feedback(c))
        c=copy.deepcopy(self.cases[1]);c['ops'][0]['fade']=0
        self.assertFalse(r.emitting_feedback(c))
        text=re.sub(r'(MRT_CASE id=0 .*? capzero=)4',r'\g<1>0',self.text)
        with self.assertRaises(AssertionError):self.validate(text)

    def test_paired_cost_records_preserve_sample_identity(self):
        for row in r.validate_pass_timings(self.text):
            self.assertEqual([p['native_first'] for p in row['pairs']],[i%2==0 for i in range(8)])
            self.assertTrue(all(p['extra_ms']==.0625 for p in row['pairs']))
        for old,new in [('variant=0 pair=0','variant=1 pair=0'),('completed_ms=1.0','completed_ms=nan')]:
            with self.assertRaises(AssertionError):r.validate_pass_timings(self.text.replace(old,new,1))


if __name__=='__main__':unittest.main()
