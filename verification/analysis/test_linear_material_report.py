"""Reject missing/duplicate/nonfinite GPU evidence and independently serialize cases."""
from dataclasses import replace
from pathlib import Path
import re
import struct
import unittest
from unittest.mock import patch
import linear_material_reference as ref
from run_linear_material import PAIRS, TIMING_PAIRS, RGB_REL_TOL, RGB_ABS_TOL, fixture_cases, binary_cases, expected, validate_report


def report():
    cases = fixture_cases()
    lines = ['CAPS mrt=4 vs_slots=512 ps_slots=512']
    for stage, shader in sorted({(stage, shader) for vs, ps in PAIRS for stage, shader in [('vs', vs), ('ps', ps)]}):
        for depth in (0, 1):
            lines.append(f'CREATE stage={stage} key={shader}_2_{depth}_1_1_1 instructions=100 words=600 completed_ms=1.25')
    for c in cases:
        lines.append(f'INVARIANT id={c["id"]} pixels=256 alpha_bad=0 motion_bad=0 depth_bad=0')
        rgba = ','.join(format(v, '.17g') for v in expected(c).encoded_rgba)
        for y in (4, 8, 12):
            for x in (4, 8, 12):
                lines.append(f'SAMPLE id={c["id"]} x={x} y={y} rgba={rgba}')
    for pair in TIMING_PAIRS:
        for lights in (0, 8):
            for i in range(18):
                mode = 2-i%3 if (i//3)%2 else i%3
                lines.append(f'TIMING pair={pair} lights={lights} mode={mode} iteration={i} draws=4 vertices=98304 width=256 completed_ms=1.5')
    lines.append(f'RESULT PASS cases={len(cases)}')
    return '\n'.join(lines)


class ReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = report()

    def test_complete_report(self):
        result = validate_report(self.text)
        self.assertEqual(result['pairs'], 20)
        self.assertEqual(result['unique_originals'], 15)
        self.assertGreater(result['hdr_channels'], 0)
        self.assertGreater(result['exact_black_channels'], 0)

    def test_inventory_and_binary_abi(self):
        cases = fixture_cases()
        data = binary_cases(cases)
        self.assertEqual(len(data), 4 + 160*len(cases))
        self.assertEqual(struct.unpack_from('<I', data)[0], len(cases))
        self.assertEqual(set((c['pair'],c['depth'],c['reverse']) for c in cases if c['label']=='pair_depth_face'),
                         {(p,d,r) for p in range(20) for d in (0,1) for r in (0,1)})
        self.assertEqual({c['lights'] for c in cases if c['label']=='lights_gains'}, {0,1,8})
        self.assertEqual(len([c for c in cases if c['label'].startswith('safety_')]), 54)

    def test_cpp_pairs_and_constant_layouts_match_oracle(self):
        text=(Path(__file__).resolve().parents[1]/'probe/linear_material_fixture.cpp').read_text()
        array=lambda name: re.search(r'\b'+name+r'\[\]\s*=\s*\{([^}]+)\}',text).group(1)
        vertices=re.findall(r'"([0-9a-f]{16})"',array('vertex_ids'))
        pixels=re.findall(r'"([0-9a-f]{16})"',array('pixel_ids'))
        vi=list(map(int,re.findall(r'\d+',array('pair_v'))))
        pi=list(map(int,re.findall(r'\d+',array('pair_p'))))
        self.assertEqual([(vertices[v],pixels[p]) for v,p in zip(vi,pi)],PAIRS)
        affine=[v=='true' for v in re.findall(r'true|false',array('pixel_affine'))]
        directions=list(map(int,re.findall(r'\d+',array('pixel_directions'))))
        self.assertEqual(affine,[ref.PROFILES[p].affine_color for p in pixels])
        self.assertEqual(directions,[ref.PROFILES[p].directions for p in pixels])

    def test_shared_lobe_cases_reject_each_argon_coefficient(self):
        witnesses={'shared_diffuse_coefficient':('diffuse_coefficient',ref.DIFFUSE_COEFFICIENT),
                   'shared_specular_power':('specular_power',5),
                   'shared_cube_coefficient':('cube_coefficient',1.)}
        cases=[c for c in fixture_cases() if c['label'] in witnesses]
        self.assertEqual(len(cases),18)
        for c in cases:
            with self.subTest(pair=c['pair'],label=c['label']):
                field,value=witnesses[c['label']]
                profile=PAIRS[c['pair']][1]
                correct=expected(c).encoded_rgba[:3]
                with patch.dict(ref.PROFILES,{profile:replace(ref.PROFILES[profile],**{field:value})}):
                    wrong=expected(c).encoded_rgba[:3]
                self.assertTrue(any(abs(a-b)>RGB_ABS_TOL+RGB_REL_TOL*abs(a)
                                    for a,b in zip(correct,wrong)))

    def test_original_inventory_retained_and_shared_boundaries_added(self):
        cases=fixture_cases()
        self.assertEqual(len([c for c in cases[:167] if c['pair']<10]),167)
        self.assertEqual(cases[166]['label'],'missing_history')
        self.assertEqual({c['pair'] for c in cases if c['label']=='shared_finite_domain'},set(range(10,16)))
        self.assertEqual([c['pair'] for c in cases if c['label']=='shared_vertex_alternation'],
                         [0,10,1,11,0,2,12,4,14,2,6,16,8,18,6])

    def test_missing_sample(self):
        lines=self.text.splitlines()
        del lines[next(i for i,l in enumerate(lines) if l.startswith('SAMPLE '))]
        with self.assertRaises(AssertionError):validate_report('\n'.join(lines))

    def test_duplicate_sample(self):
        lines=self.text.splitlines()
        i=next(i for i,l in enumerate(lines) if l.startswith('SAMPLE '))
        lines[i+1]=lines[i]
        with self.assertRaises(AssertionError):validate_report('\n'.join(lines))

    def test_nonfinite_oracle_mismatch_and_black_sign(self):
        for value in ('nan','999','-0'):
            lines=self.text.splitlines()
            case = next(c for c in fixture_cases() if c['label']=='safety_material_black')
            i=next(i for i,l in enumerate(lines) if l.startswith(f'SAMPLE id={case["id"]} '))
            head,rgba=lines[i].split('rgba=')
            lines[i]=head+'rgba='+value+','+rgba.split(',',1)[1]
            with self.assertRaises(AssertionError):validate_report('\n'.join(lines))

    def test_missing_creation_and_invariant(self):
        for prefix in ('CREATE ', 'INVARIANT '):
            lines=self.text.splitlines()
            del lines[next(i for i,l in enumerate(lines) if l.startswith(prefix))]
            with self.assertRaises(AssertionError):validate_report('\n'.join(lines))

    def test_material_strength_not_exponentiated(self):
        cases=[c for c in fixture_cases() if c['label']=='native_material_strength']
        one, four = expected(cases[1]).linear_rgb, expected(cases[2]).linear_rgb
        for a,b in zip(one,four):self.assertAlmostEqual(b,4*a)

    def test_reject_clamped_hdr(self):
        lines=self.text.splitlines()
        i=next(i for i,l in enumerate(lines) if l.startswith('SAMPLE ') and any(float(v)>1 for v in l.split('rgba=')[1].split(',')[:3]))
        head,rgba=lines[i].split('rgba=')
        lines[i]=head+'rgba='+','.join(str(min(float(v),1)) for v in rgba.split(','))
        with self.assertRaises(AssertionError):validate_report('\n'.join(lines))


if __name__ == '__main__':unittest.main()
