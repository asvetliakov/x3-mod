"""The report must preserve negative parity/capability outcomes and exact scope."""
from pathlib import Path
import re
import tempfile
import unittest
import run_linear_emission_sm1 as r
from source_text import source_text


def witness(failure=None,unsupported=None):
    lines=['SM1_CAPS ps=ffff0300 vs=fffe0300 mrt=4 ps1_max=8 independent_masks=1 post_blend=1']
    parity=emission=missing=0
    for p in range(9):
        for m in range(6):
            for g in range(4):
                failed=(p,m,g)==unsupported
                lines.append(f'SM1_CREATE pair={p} mode={m} gain_index={g} hr={"8876086c" if failed else "00000000"} words=153 ms=.1')
        lines.append(f'SM1_PROJECTED pair={p} flags=00000104 enhanced_draws=0 admitted=0 reason=undefined_source_w')
        for c,name in enumerate(r.CASES):
            for m in range(6):
                prefix=f'SM1_CASE pair={p} case={c} name={name} mode={m}'
                g=1 if c==19 else 2 if c==25 else 3 if c==30 else 0
                if (p,m,g)==unsupported:
                    missing+=1;lines.append(prefix+f' status=unsupported hr=8876086c boundary={int(c in (22,30))}');continue
                bad=failure and failure[:3]==(p,c,m)
                counts=dict(rgb_diff=0,alpha_diff=0,e_diff=0,mask_diff=0)
                if bad:counts[failure[3]]=1
                parity+=bool(counts['rgb_diff'] or counts['alpha_diff']);emission+=bool(counts['e_diff'] or counts['mask_diff'])
                lines.append(prefix+' status=measured '+' '.join(f'{k}={v}' for k,v in counts.items())+f' survived={250 if c==17 else 550 if c==16 else 800} max_native=0 max_energy_fraction=0 boundary={int(c in (22,30))}')
                if c==29 and m%3:lines.append(f'SM1_OVERLAP pair={p} mode={m} dip=1 triangles=4 layers=2 actual_e=.25 shared_e=.25 q_e=.23 gap=.02 q_policy_implemented=0')
                if bad:lines.append(f'SM1_WITNESS pair={p} case={c} mode={m} prefix=failure_p{p}_c{c}_m{m} native_lane=0 native_bits=3800 promoted_bits=3801')
        if p==4:lines.append('SM1_RESET passed=1')
    lines.append(f'SM1_COMPLETE pairs=9 cases={len(r.CASES)} rows={9*len(r.CASES)*6} unsupported={missing} parity_failures={parity} emission_failures={emission} reset=1')
    return '\n'.join(lines)


class Sm1ReportTests(unittest.TestCase):
    def test_all_nine_six_modes_and_boundary_separation(self):
        report=r.validate(witness())
        self.assertEqual((report['pairs'],report['original_vs'],report['original_ps'],report['rows']),(9,9,6,1674))
        self.assertTrue(report['all_modes_qualified_in_range'])
        self.assertTrue(all(x['measured']==279 for x in report['modes']))

    def test_canonical_record_is_compact_and_screen_policy_stays_unimplemented(self):
        measured=r.validate(witness());compact=r.compact_report(measured)
        self.assertIn('case_results',measured);self.assertNotIn('case_results',compact)
        self.assertEqual(compact['rows'],1674)
        self.assertEqual(compact['gains'],[1.,0.,2.5,.25])
        self.assertEqual(compact['promoted_creations'],216)
        self.assertTrue(all(x['q_policy_implemented']=='0' for x in compact['overlap_diagnostics']))
        self.assertEqual(len(compact['overlap_diagnostics']),36)

    def test_native_parity_failure_is_a_completed_negative_result(self):
        for lane in ('rgb_diff','alpha_diff'):
            report=r.validate(witness((3,1,1,lane)))
            self.assertTrue(report['completed']);self.assertFalse(report['all_modes_qualified_in_range'])
            self.assertEqual(report['parity_failures'],1)
            self.assertFalse(report['modes'][1]['native_in_range_exact'])
            self.assertTrue(report['modes'][4]['native_in_range_exact'])

    def test_hdr_failure_is_reported_separately_from_in_range(self):
        report=r.validate(witness((0,22,0,'rgb_diff')))
        self.assertTrue(report['modes'][0]['native_in_range_exact'])
        self.assertFalse(report['modes'][0]['boundary_exact'])

    def test_emission_and_mask_failures_cannot_hide_behind_native_parity(self):
        for lane in ('e_diff','mask_diff'):
            report=r.validate(witness((1,18,5,lane)))
            self.assertTrue(report['modes'][5]['native_in_range_exact'])
            self.assertFalse(report['modes'][5]['qualified_in_range'])

    def test_creation_failure_continues_other_modes(self):
        report=r.validate(witness(unsupported=(2,3,0)))
        self.assertEqual(report['unsupported'],28)
        self.assertFalse(report['modes'][3]['qualified_in_range'])
        self.assertTrue(report['modes'][0]['qualified_in_range'])
        self.assertEqual(report['rows'],1674)

    def test_alpha_boundaries_require_distinct_nonzero_native_survivors(self):
        text=witness()
        for c,count in ((16,0),(17,0),(16,250),(17,550),(16,800),(17,800)):
            line=next(row for row in text.splitlines()
                      if row.startswith(f'SM1_CASE pair=8 case={c} ') and ' mode=5 ' in row)
            changed=re.sub(r'survived=\d+',f'survived={count}',line)
            with self.subTest(case=c,survived=count),self.assertRaises(AssertionError):
                r.validate(text.replace(line,changed,1))

    def test_missing_duplicate_wrong_layout_or_projected_draw_rejected(self):
        text=witness()
        changes=[('name=flat','name=clip'),('enhanced_draws=0','enhanced_draws=1'),
                 ('admitted=0','admitted=1'),('flags=00000104','flags=00000004'),
                 ('SM1_RESET passed=1',''),('rows=1674','rows=1673'),
                 ('survived=800','survived=0'),('max_native=0','max_native=nan'),('dip=1','dip=2'),('q_policy_implemented=0','q_policy_implemented=1'),('gap=.02','gap=0')]
        for old,new in changes:
            with self.subTest(old=old),self.assertRaises(AssertionError):r.validate(text.replace(old,new,1))
        line=next(x for x in text.splitlines() if x.startswith('SM1_CASE '))
        for replacement in ('',line+'\n'+line):
            with self.assertRaises(AssertionError):r.validate(text.replace(line,replacement,1))

    def test_negative_result_needs_raw_failure_witness(self):
        text=witness((0,0,0,'rgb_diff'))
        line=next(x for x in text.splitlines() if x.startswith('SM1_WITNESS '))
        with self.assertRaises(AssertionError):r.validate(text.replace(line,''))
        report=r.validate(text)
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder)
            with self.assertRaises(AssertionError):r.validate_witnesses(work,report)
            for m in (1,2,4,5):
                for kind in ('emission','native'):(work/f'overlap_mode{m}_{kind}.rgba16f').write_bytes(bytes(8192))
            names=['native','actual0','reference0','reference1','reference2']
            for name in names:(work/f'failure_p0_c0_m0_{name}.rgba16f').write_bytes(bytes(8192))
            r.validate_witnesses(work,report)
            (work/'failure_p0_c0_m0_native.rgba16f').write_bytes(bytes(8190))
            with self.assertRaises(AssertionError):r.validate_witnesses(work,report)

    def test_fixture_exact_inventory_and_independent_transfer_oracle(self):
        source=source_text(r.ROOT/'verification/probe/linear_emission_sm1_fixture.cpp')
        table=source.split('constexpr Pair pairs[] = {',1)[1].split('};',1)[0]
        rows=tuple((a,b,int(c)) for a,b,c in re.findall(r'\{"([0-9a-f]{16})", "([0-9a-f]{16})", (\d)\}',table))
        self.assertEqual(rows,r.PAIRS)
        table=source.split('constexpr const char *names[] = {',1)[1].split('};',1)[0]
        self.assertEqual(tuple(re.findall(r'"([a-z0-9_]+)"',table)),r.CASES)
        sample_shader=source.split('Words witness(',1)[1].split('using Image',1)[0]
        self.assertNotRegex(sample_shader,r'ins\(w,\s*(32|5|88),')
        self.assertIn('std::pow(sample, 2.2)',source)
        self.assertIn('caps.PixelShader1xMaxValue',source)
        self.assertIn('D3DTTFF_COUNT4 | D3DTTFF_PROJECTED',source)
        self.assertIn('c == 26 ? .5f',source)
        script=source_text(r.ROOT/'verification/probe/build_linear_emission_sm1.sh')
        self.assertNotIn('d3d9.dll',script)
        self.assertIn('-mstackrealign -mincoming-stack-boundary=2',script)


if __name__=='__main__':unittest.main()
