"""Packed mathematical qualification must retain ordering/domain/resource gates."""
import re
import tempfile
from pathlib import Path
import unittest
import run_linear_emission_sm1_packed as r

HELPERS=('PACKED_HELPER name=initialize words=80 alu=22 tex=1 temps=2 samplers=1 outputs=15 constants=1',
         'PACKED_HELPER name=assemble_b words=60 alu=5 tex=4 temps=5 samplers=4 outputs=1 constants=0',
         'PACKED_HELPER name=assemble_c words=140 alu=26 tex=5 temps=7 samplers=5 outputs=1 constants=1')

def caps(w=32,h=32):return f'PACKED_CAPS width={w} height={h} mrt=4 masks=1 post_blend=1 src_one=1 dst_invsrcalpha=1 ps1_max=8'
def witness(failure=None,unsupported=None):
    lines=[caps(),*HELPERS];failures=boundary_failures=missing=order=0;first=set()
    for p in range(9):
        for g in range(4):lines.append(f'PACKED_CREATE pair={p} gain_index={g} words=201 hr={"8876086c" if unsupported==(p,g) else "00000000"}')
        for c,name in enumerate(r.CASES):
            for s in range(3):
                base=f'PACKED_CASE pair={p} case={c} name={name} schedule={s}'
                if unsupported==(p,r.gain_index(c)):
                    missing+=1;lines.append(base+f' status=unsupported hr=8876086c boundary={int(c in r.BOUNDARIES)}');continue
                counts={k:0 for k in r.ERRORS}
                bad=failure and failure[:3]==(p,c,s)
                if bad:counts[failure[3]]=1
                boundary=c in r.BOUNDARIES
                if bad:
                    if boundary:boundary_failures+=1
                    else:failures+=1
                survived=0 if c in (9,13) else 700 if c in (7,10,12) else 1400
                overlap=500 if survived==1400 else 0
                changed=100 if s==2 else 0
                if c==0:order+=changed
                lines.append(base+f' status=measured boundary={int(boundary)} '+' '.join(f'{k}={v}' for k,v in counts.items())+
                             f' surviving={survived} overlap={overlap} order_changed={changed} flag_zero_changed=0 unchanged_covered={700 if c in (1,2) else 0} max_fraction=0 original_dips={2 if s==1 else 1} packed_dips={2 if s==1 else 1}')
                if bad and boundary not in first:
                    first.add(boundary);lines.append(f'PACKED_WITNESS pair={p} case={c} schedule={s} boundary={int(boundary)} prefix=failure_p{p}_c{c}_s{s}')
                if p==0 and c==16 and s==0:lines.append('PACKED_RANGE_WITNESS prefix=range_q2 q_clamped_by_fixture=0 qualification=0')
        if p==4:lines.append('PACKED_RESET passed=1')
    lines.append(f'PACKED_COMPLETE pairs=9 cases=24 schedules=3 rows=648 unsupported={missing} failures={failures} boundary_failures={boundary_failures} order_changed={order} reset=1 owned_targets=8 target_bytes=65536 live_publication=0')
    return '\n'.join(lines)


class PackedReportTests(unittest.TestCase):
    def test_complete_all_nine_domain_and_compact_resource_report(self):
        report=r.validate(witness())
        self.assertTrue(report['qualified_in_domain']);self.assertFalse(report['live_publication'])
        self.assertEqual((report['pairs'],report['rows'],report['creations']),(9,648,36))
        self.assertEqual(len(report['pair_schedule_results']),27)
        self.assertNotIn('case_results',report)
        self.assertEqual(report['gains'],(1.,0.,.25,2.5))
        self.assertEqual(len(report['helpers']),3)

    def test_native_alpha_plane_mask_or_unchanged_failure_is_not_qualified(self):
        for field in r.ERRORS:
            s=1 if field=='same_dip_diff' else 0
            with self.subTest(field=field):
                report=r.validate(witness((0,1,s,field)))
                self.assertTrue(report['completed']);self.assertFalse(report['qualified_in_domain'])
                self.assertEqual(report['failures'],1)

    def test_signed_range_overflow_are_operational_not_new_domains(self):
        for c in r.BOUNDARIES:
            report=r.validate(witness((0,c,0,'nonfinite')))
            self.assertTrue(report['qualified_in_domain'])
            self.assertEqual(report['boundary_failures'],1)
            self.assertFalse(report['live_publication'])
        text=witness()
        with self.assertRaises(AssertionError):r.validate(text.replace('q_clamped_by_fixture=0','q_clamped_by_fixture=1'))
        with self.assertRaises(AssertionError):r.validate(text.replace('qualification=0','qualification=1'))

    def test_creation_failure_preserves_other_pair_measurements(self):
        report=r.validate(witness(unsupported=(3,0)))
        self.assertTrue(report['completed']);self.assertFalse(report['qualified_in_domain'])
        self.assertGreater(report['unsupported'],0)
        self.assertEqual(report['pair_schedule_results'][0]['unsupported'],0)

    def test_missing_repeated_rows_or_absent_real_controls_rejected(self):
        text=witness()
        line=next(x for x in text.splitlines() if x.startswith('PACKED_CASE '))
        for replacement in ('',line+'\n'+line):
            with self.assertRaises(AssertionError):r.validate(text.replace(line,replacement,1))
        for c,key,value in ((0,'overlap',0),(0,'order_changed',0),(7,'surviving',0),
                            (7,'surviving',1400),(8,'surviving',700),(9,'surviving',700),
                            (11,'surviving',0),(12,'surviving',1400),(1,'unchanged_covered',0)):
            s=2 if key=='order_changed' else 0
            line=next(x for x in text.splitlines() if x.startswith(f'PACKED_CASE pair=0 case={c} ') and f' schedule={s} ' in x)
            changed=re.sub(rf'{key}=\d+',f'{key}={value}',line)
            with self.subTest(case=c,key=key),self.assertRaises(AssertionError):r.validate(text.replace(line,changed,1))
        for old,new in (('packed_dips=1','packed_dips=2'),('masks=1','masks=0'),('post_blend=1','post_blend=0'),('PACKED_RESET passed=1',''),('live_publication=0','live_publication=1'),('max_fraction=0','max_fraction=nan')):
            with self.subTest(old=old),self.assertRaises(AssertionError):r.validate(text.replace(old,new,1))

    def test_raw_failures_and_unconditional_q2_witness_required(self):
        text=witness((0,0,0,'b_diff'));report=r.validate(text)
        line=next(x for x in text.splitlines() if x.startswith('PACKED_WITNESS '))
        with self.assertRaises(AssertionError):r.validate(text.replace(line,''))
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder)
            with self.assertRaises(AssertionError):r.validate_witnesses(work,report)
            for prefix in ('failure_p0_c0_s0','range_q2'):
                for name in r.witness_names(prefix):(work/name).write_bytes(bytes(8192))
            r.validate_witnesses(work,report)
            (work/'range_q2_plane0.rgba16f').write_bytes(bytes(8190))
            with self.assertRaises(AssertionError):r.validate_witnesses(work,report)

    def test_five_separate_timing_phases_and_target_bytes(self):
        text='\n'.join([caps(1920,1080),*HELPERS]+[f'PACKED_TIMING sample={i} phase={phase} ms=.1' for i in range(8) for phase in r.PHASES])
        text+='\nPACKED_BENCH_COMPLETE samples=8 warmups=4 width=1920 height=1080 original_dips=1 packed_dips=1 init_draws=1 b_draws=1 c_draws=1 layers=2 owned_targets=8 target_bytes=132710400 live_publication=0'
        report=r.validate_timing(text);self.assertTrue(report['supported']);self.assertEqual(set(report['median_ms']),set(r.PHASES))
        for old,new in (('phase=assemble_b','phase=assemble_c'),('target_bytes=132710400','target_bytes=99532800'),('original_dips=1','original_dips=2'),('ms=.1','ms=nan')):
            with self.assertRaises(AssertionError):r.validate_timing(text.replace(old,new,1))
        refused='\n'.join([caps(1920,1080),*HELPERS,'PACKED_BENCH_UNSUPPORTED hr=8876086c'])
        self.assertFalse(r.validate_timing(refused)['supported'])

    def test_authored_case_inventory_and_exact_zero_shader_predicate(self):
        source=(r.ROOT/'verification/probe/linear_emission_sm1_packed_fixture.cpp').read_text()
        pairs=source.split('constexpr Pair pairs[] = {',1)[1].split('};',1)[0]
        self.assertEqual(tuple((a,b,int(c)) for a,b,c in re.findall(r'\{"([0-9a-f]{16})", "([0-9a-f]{16})", (\d)\}',pairs)),r.PAIRS)
        names=source.split('constexpr const char *cases[] = {',1)[1].split('};',1)[0]
        self.assertEqual(tuple(re.findall(r'"([a-z0-9_]+)"',names)),r.CASES)
        self.assertIn('ins(w, 35, {dst(0, 6, 4), src(0, c, 0xaa)});',source)
        self.assertIn('src(0, 6, 0xaa) | 0x1000000u',source)
        self.assertIn('masks(8, 7)',source)
        self.assertIn('packed && !measuring ? 9 : 15',source)
        self.assertIn('packed && !measuring ? 7 : 15',source)
        self.assertIn('Interval expected_linear{linear, linear}',source)
        self.assertIn('pixel_variant',source)
        build=(r.ROOT/'verification/probe/build_linear_emission_sm1_packed.sh').read_text()
        self.assertNotIn('d3d9.dll',build)
        self.assertIn('-mstackrealign -mincoming-stack-boundary=2',build)


if __name__=='__main__':unittest.main()
