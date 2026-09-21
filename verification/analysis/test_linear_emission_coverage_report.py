"""Three-output coverage producer proof/report; no game shader assets."""
from verification.analysis.retired_tests import load_tests  # retired feature: hidden from default discovery
import copy
import struct
import unittest
import run_linear_emission as r


def report(cases):
    lines=['CAPS vs=fffe0300 ps=ffff0300 rt=4']
    lines += [f'FORMAT name={n} hr=00000000' for n in ('fp16_rt','fp16_blend','d24s8')]
    lines += ['DEPTH_MATCH format=113 hr=00000000','MRT_CAPS slots=4 postblend=1 independent_masks=0']
    data=bytearray()
    for c in cases:
        color,depth,count,native,energy=r.mrt_expected(c,True)
        lines.append('MRT_CASE id='+str(c['id'])+' '+' '.join(f'{k}={v}' for k,v in count.items()))
        lines.append(f"COVERAGE_CASE id={c['id']} parity={count['bursts']*2048}")
        data+=struct.pack('<I',c['id'])
        mask=[v for x in r.coverage_expected(c) for v in (x,x,x,0.)]
        for values in ([v for p in color for v in p],depth,[v for p in native for v in p],[v for p in energy for v in p],mask):
            data+=struct.pack('<'+str(len(values))+'f',*values)
    for w,h in ((1280,768),(1920,1080)):
        for pair in range(8):
            for order in range(2):
                variant=1-order if pair%2 else order
                lines.append(f'COVERAGE_TIMING width={w} height={h} variant={variant} pair={pair} order={order} completed_ms={1+pair*.125+variant*.0625}')
            lines.append(f'COVERAGE_TIMING width={w} height={h} variant=2 pair={pair} order=0 completed_ms=.25')
    lines.append(f'COVERAGE_RESULT pass cases={len(cases)} shaders=381')
    return '\n'.join(lines)+'\n',bytes(data)


class CoverageReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cases=r.coverage_cases();cls.text,cls.data=report(cls.cases)

    def test_original_prefix_and_three_output_record(self):
        self.assertEqual(self.cases[:70],r.original_cases()[:70])
        result=r.validate_coverage_report(self.text,self.data,self.cases)
        self.assertEqual(result['cases'],376)
        self.assertEqual(result['shader_creations'],381)
        self.assertEqual(result['coverage_variants'],50)
        self.assertEqual(result['exact_two_three_output_channels'],result['invariants']['bursts']*2048)
        self.assertEqual(result['covered_pixels']+result['uncovered_pixels'],376*256)
        self.assertEqual(result['max_energy_tolerance_fraction'],0)

    def test_zero_color_fade_gain_and_alpha_still_have_coverage(self):
        for c in self.cases:
            if c['label'] in ('coverage_zero_rgb_alpha','coverage_zero_fade_alpha'):
                self.assertEqual(r.coverage_expected(c),[1.]*256)
        c=copy.deepcopy(self.cases[0])
        for o in c['ops']:o['gain']=0
        self.assertGreater(sum(r.coverage_expected(c)),0)
        self.assertTrue(all(p[:3]==[0.,0.,0.] for p in r.mrt_expected(c,True)[4]))

    def test_persistence_and_next_frame_clear_are_distinct(self):
        for n in (2,16):
            c=next(c for c in self.cases if c['label']=='coverage_persistent_'+str(n))
            mask=r.coverage_expected(c)
            self.assertEqual(max(mask),n)
            self.assertIn(n/2,mask)
            self.assertIn(0,mask)
            self.assertEqual(r.mrt_expected(c)[2]['bursts'],n)
        self.assertEqual(r.coverage_expected(self.cases[80]),[0.]*256)

    def test_alpha_depth_scissor_and_viewport_limit_union(self):
        for label in ('original_sampled_alpha','original_alpha_test'):
            c=next(c for c in self.cases if c['label']==label)
            mask=r.coverage_expected(c)
            self.assertGreater(sum(mask),0)
            self.assertLess(sum(v>0 for v in mask),256)
        c=copy.deepcopy(self.cases[80]);c['ops'][0]['z']=.4
        self.assertEqual(r.coverage_expected(c),[1.]*256)
        c['mask']=1;c['ops'][0]['color'][3]=0
        self.assertEqual(r.coverage_expected(c),[0.]*256)

    def test_requires_three_slots_but_no_independent_mask_cap(self):
        self.assertFalse(r.validate_coverage_report(self.text,self.data,self.cases)['mrt_caps']['independent_write_masks'])
        for text in (self.text.replace('slots=4','slots=2'),self.text.replace('postblend=1','postblend=0'),self.text.replace('shaders=381','shaders=380')):
            with self.assertRaises(AssertionError):r.validate_coverage_report(text,self.data,self.cases)

    def test_rejects_coverage_erasure_false_positive_and_parity_loss(self):
        for case,pixel in ((70,0),(80,0)):
            raw=bytearray(self.data);offset=case*(4+256*17*4)+4+256*13*4+pixel*16
            struct.pack_into('<f',raw,offset,0. if case==70 else 1.)
            with self.assertRaises(AssertionError):r.validate_coverage_report(self.text,raw,self.cases)
        with self.assertRaises(AssertionError):r.validate_coverage_report(self.text.replace('parity=2048','parity=2047',1),self.data,self.cases)

    def test_mask_alpha_is_not_a_payload(self):
        raw=bytearray(self.data)
        struct.pack_into('<f',raw,4+256*13*4+12,123.)
        self.assertEqual(r.validate_coverage_report(self.text,raw,self.cases)['cases'],376)

    def test_paired_cost_and_clear_identity(self):
        for row in r.validate_coverage_timings(self.text):
            self.assertEqual(row['paired_extra_output_median_ms'],.0625)
            self.assertEqual(row['frame_clear_median_ms'],.25)
            self.assertEqual([p['two_first'] for p in row['pairs']],[i%2==0 for i in range(8)])
        for text in (self.text.replace('variant=0 pair=0','variant=1 pair=0',1),self.text.replace('completed_ms=1.0','completed_ms=nan',1)):
            with self.assertRaises(AssertionError):r.validate_coverage_timings(text)


if __name__=='__main__':unittest.main()
