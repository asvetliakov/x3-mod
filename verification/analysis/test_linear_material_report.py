"""Reject missing/duplicate/nonfinite GPU evidence and independently serialize cases."""
from dataclasses import replace
from pathlib import Path
import hashlib
import json
import re
import struct
import unittest
from unittest.mock import patch
import linear_material_reference as ref
from run_linear_material import BOUNDARY, CUBE_PATTERN, cube_sample, cube_location, expected_alpha, DETAIL_PATTERN, detail_sample, FIXED_VERTICES, PAIRS, TIMING_PAIRS, RGB_REL_TOL, RGB_ABS_TOL, fixture_cases, binary_cases, expected, validate_report


def report():
    cases = fixture_cases()
    lines = ['CAPS mrt=4 vs_slots=512 ps_slots=512']
    for stage, shader in sorted({(stage, shader) for vs, ps in PAIRS for stage, shader in [('vs', vs), ('ps', ps)]}):
        for depth in (0, 1):
            lines.append(f'CREATE stage={stage} key={shader}_2_{depth}_1_1_1 instructions=100 words=600 completed_ms=1.25')
    for c in cases:
        lines.append(f'INVARIANT id={c["id"]} pixels=256 alpha_bad=0 motion_bad=0 depth_bad=0 rgb_bad=0')
        values=(.5,1.,.25,expected_alpha(c)) if c['flags'] & BOUNDARY else expected(c).encoded_rgba
        rgba = ','.join(format(v, '.17g') for v in values)
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
        self.assertEqual(result['pairs'], 116)
        self.assertEqual(result['unique_originals'], 83)
        self.assertGreater(result['hdr_channels'], 0)
        self.assertGreater(result['exact_black_channels'], 0)

    def test_inventory_and_binary_abi(self):
        cases = fixture_cases()
        data = binary_cases(cases)
        self.assertEqual(len(data), 4 + 240*len(cases))
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
        self.assertEqual(affine,[ref.PROFILES[p].affine_color if p in ref.PROFILES else False for p in pixels])
        self.assertEqual(directions,[(ref.PROFILES.get(p) or ref.ASTEROID_PROFILES[p]).directions for p in pixels])
        bump=[v=='true' for v in re.findall(r'true|false',array('pixel_bump'))]
        app=[v=='true' for v in re.findall(r'true|false',array('pixel_application'))]
        self.assertEqual(bump,[(ref.PROFILES.get(p) or ref.ASTEROID_PROFILES[p]).bump_map for p in pixels])
        self.assertEqual(app,[ref.PROFILES[p].application_coefficients if p in ref.PROFILES else False for p in pixels])

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

    def test_cube_face_axes_and_independent_offcenter_orientation(self):
        axes=((1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1))
        for face,direction in enumerate(axes):
            self.assertEqual(cube_location(direction),(face,.5,.5))
            self.assertEqual(cube_sample(direction),((face+1)/8.,.25,.375))
        offcenter=(((1,.75,-.75),(.125,.5,.125)),((-1,-.75,.75),(.25,.5,.75)),
                   ((.75,1,-.75),(.375,.5,.125)),((-.75,-1,.75),(.5,.125,.125)),
                   ((.75,-.75,1),(.625,.5,.75)),((-.75,.75,-1),(.75,.5,.125)))
        for direction,color in offcenter:self.assertEqual(cube_sample(direction),color)
        for unsafe in ((1,1,0),(1,.5,0),(0,0,0)):
            with self.assertRaises(ValueError):cube_sample(unsafe)

    def test_bump_inventory_and_stable_cube_domain(self):
        cases=fixture_cases()
        self.assertEqual(len(cases),2757)
        self.assertTrue(all(c['pair']<20 for c in cases[:313]))
        self.assertEqual({(c['pair'],c['depth'],c['reverse']) for c in cases if c['label']=='bump_pair_depth_face'},
                         {(p,d,r) for p in range(20,30) for d in (0,1) for r in (0,1)})
        self.assertEqual(sum(bool(c['flags']&BOUNDARY) for c in cases),48)
        for c in cases:
            if c['flags']&BOUNDARY:continue
            a,b=expected(c),expected(c,True)
            self.assertEqual(a.encoded_rgba[3],expected_alpha(c))
            if c['flags']&CUBE_PATTERN:
                # Different endpoint precision must not select a different
                # discrete cube color. Other _pp arithmetic gets RGB tolerance.
                def geometry(half):
                    profile=ref.PROFILES[PAIRS[c['pair']][1]]
                    return ref.bump_geometry(c['normal_sample'],c['tangent'],c['binormal'],c['normal'],c['camera'],
                                             two_sided=profile.two_sided,face=-1 if c['reverse'] else 1,half_source=half,
                                             normal_encoding=profile.normal_encoding)
                self.assertEqual(cube_sample(geometry(False).reflection),cube_sample(geometry(True).reflection))

    def test_unused_channels_and_geometric_point_response(self):
        cases=fixture_cases()
        for pair in range(20,26):
            a=next(c for c in cases if c['pair']==pair and c['label']=='bump_alpha_binormal')
            b=next(c for c in cases if c['pair']==pair and c['label']=='bump_unused_red_blue')
            self.assertEqual(expected(a),expected(b))
            a,b=[c for c in cases if c['pair']==pair and c['label']=='bump_cube_perturbed']
            self.assertNotEqual(expected(a),expected(b))
        for pair in (20,22,26):
            a,b=[c for c in cases if c['pair']==pair and c['label']=='bump_geometric_point']
            self.assertEqual(expected(a),expected(b))

    def test_boundary_explicitly_excludes_rgb_oracle_but_enforces_storage(self):
        c=next(c for c in fixture_cases() if c['flags']&BOUNDARY)
        with self.assertRaises(ValueError):expected(c)
        for values in ((123.,0.,4.),(-.1,0.,0.),(155.,0.,0.)):
            lines=self.text.splitlines()
            for i,line in enumerate(lines):
                if line.startswith(f'SAMPLE id={c["id"]} '):
                    lines[i]=line.split('rgba=')[0]+'rgba='+','.join(map(str,(*values,expected_alpha(c))))
            if values[0]==123.:self.assertEqual(validate_report('\n'.join(lines))['boundary_cases'],48)
            else:
                with self.assertRaises(AssertionError):validate_report('\n'.join(lines))

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

    def test_old_binary_payload_prefix_exact_and_new_scalar_tail(self):
        cases=fixture_cases()
        data=binary_cases(cases[:512])
        legacy=bytearray(data[:4])
        for i in range(512):legacy.extend(data[4+240*i:4+240*i+224])
        self.assertEqual(hashlib.sha256(legacy).hexdigest(),
                         'edb4bfc95b39fa04c365abae102471c7b623cb64da36108cde63374b6632d7f5')
        c=next(c for c in cases if c['label']=='standard_coeff_mixed')
        tail=struct.unpack_from('<4f',binary_cases([c]),4+224)
        self.assertEqual(tail,(2.5,.75,1.25,2.5))

    def test_expanded_pair_and_required_case_coverage(self):
        proof=json.loads((Path(__file__).resolve().parents[2]/'docs/reverse-engineering/linear-material-profiles.json').read_text())
        self.assertEqual(set(PAIRS),{(p['vs'],p['ps']) for p in proof['pairs']})
        self.assertEqual(len(PAIRS),116)
        cases=fixture_cases()
        self.assertEqual({(c['pair'],c['depth'],c['reverse']) for c in cases if c['label']=='extended_pair_depth_face'},
                         {(p,d,r) for p in range(30,70) for d in (0,1) for r in (0,1)})
        self.assertEqual({c['pair'] for c in cases if c['label']=='extended_missing_history'},set(range(30,70)))
        for p in range(30,70):
            self.assertEqual({tuple(c['gains']) for c in cases if c['pair']==p and c['label']=='extended_independent_gains'},
                             {(0.,0.,0.),(4.,1.,1.),(1.,16.,1.),(1.,1.,16.)})
        for start in (30,40,50,60):
            for p in (start,start+2,start+6):
                self.assertEqual({c['lights'] for c in cases if c['pair']==p and c['label']=='extended_lights_gains'},
                                 {1} if p==start+6 else {0,1,8})
        self.assertEqual(len({PAIRS[c['pair']][1] for c in cases if c['label']=='standard_coeff_mixed'}),18)

    def test_new_coefficient_cases_reject_swapped_fixed_and_decoded_controls(self):
        cases=fixture_cases()
        for c in (c for c in cases if c['label']=='standard_coeff_mixed'):
            correct=expected(c).encoded_rgba[:3]
            alternatives=([1.,1.,1.,10.], [.75,2.5,1.25,2.5],
                          [2.5**2.2,.75**2.2,1.25**2.2,2.5])
            for coeff in alternatives:
                wrong=expected(dict(c,coefficients=coeff)).encoded_rgba[:3]
                self.assertTrue(any(abs(a-b)>RGB_ABS_TOL+RGB_REL_TOL*abs(a) for a,b in zip(correct,wrong)),(c['pair'],coeff))
        for c in (c for c in cases if c['label']=='split_specular_power'):
            p=PAIRS[c['pair']][1]
            correct=expected(c).encoded_rgba[:3]
            with patch.dict(ref.PROFILES,{p:replace(ref.PROFILES[p],specular_power=6)}):wrong=expected(c).encoded_rgba[:3]
            self.assertTrue(any(abs(a-b)>RGB_ABS_TOL+RGB_REL_TOL*abs(a) for a,b in zip(correct,wrong)))

    def test_low_witnesses_reject_ag_reconstruction_and_ignore_alpha(self):
        cases=fixture_cases()
        for p in range(60,66):
            c=next(c for c in cases if c['pair']==p and c['label']=='extended_normal_negative_z')
            correct=expected(c).encoded_rgba[:3]
            profile=PAIRS[p][1]
            with patch.dict(ref.PROFILES,{profile:replace(ref.PROFILES[profile],normal_encoding='ag')}):
                wrong=expected(c).encoded_rgba[:3]
            self.assertTrue(any(abs(a-b)>RGB_ABS_TOL+RGB_REL_TOL*abs(a) for a,b in zip(correct,wrong)))
            a=next(c for c in cases if c['pair']==p and c['label']=='extended_normal_binormal')
            b=next(c for c in cases if c['pair']==p and c['label']=='extended_normal_unused')
            self.assertEqual(expected(a),expected(b))
            a,b=[c for c in cases if c['pair']==p and c['label']=='extended_geometric_point']
            self.assertEqual(expected(a),expected(b))

    def test_runner_requires_explicit_executable_and_never_builds(self):
        text=(Path(__file__).resolve().parents[1]/'probe/run_linear_material.py').read_text()
        self.assertIn("parser.add_argument('--exe', type=Path, required=True",text)
        self.assertNotIn('build_linear_material.sh',text)

    def test_previous_1527_binary_payloads_are_exact(self):
        self.assertEqual(hashlib.sha256(binary_cases(fixture_cases()[:1527])).hexdigest(),
                         'cd0e072d83bee387977252e8cbe990e515ba01332e0582f4afd36335c4da66cd')

    def test_remaining_hull_complete_pairs_controls_and_programs(self):
        cases=fixture_cases()
        self.assertEqual({(c['pair'],c['depth'],c['reverse']) for c in cases if c['label']=='hull_pair_depth_face'},
                         {(p,d,r) for p in range(70,110) for d in (0,1) for r in (0,1)})
        self.assertEqual({c['pair'] for c in cases if c['label']=='hull_missing_history'},set(range(70,110)))
        for p in range(70,110):
            self.assertEqual({tuple(c['gains']) for c in cases if c['pair']==p and c['label']=='hull_independent_gains'},
                             {(0.,0.,0.),(4.,1.,1.),(1.,16.,1.),(1.,1.,16.)})
        for start in (70,80,90,100):
            for p in (start,start+2,start+6):
                self.assertEqual({c['lights'] for c in cases if c['pair']==p and c['label']=='hull_lights_gains'},
                                 {1} if p==start+6 else {0,1,8})
        for label in ('hull_diffuse_coefficient','hull_specular_power','hull_cube_coefficient','hull_affine_angular',
                      'hull_packed_angular','hull_finite_domain'):
            self.assertEqual(len({PAIRS[c['pair']][1] for c in cases if c['label']==label}),24)

    def test_hull_coefficient_witnesses_reject_neighbor_family_constants(self):
        for c in fixture_cases():
            if c['label'] not in ('hull_diffuse_coefficient','hull_specular_power','hull_cube_coefficient'):continue
            p=PAIRS[c['pair']][1]
            field={'hull_diffuse_coefficient':'diffuse_coefficient','hull_specular_power':'specular_power',
                   'hull_cube_coefficient':'cube_coefficient'}[c['label']]
            actual=getattr(ref.PROFILES[p],field)
            wrong_value=(6 if actual!=6 else 5) if field=='specular_power' else (.5 if actual==1 else 1.)
            correct=expected(c).encoded_rgba[:3]
            with patch.dict(ref.PROFILES,{p:replace(ref.PROFILES[p],**{field:wrong_value})}):
                wrong=expected(c).encoded_rgba[:3]
            self.assertTrue(any(abs(a-b)>RGB_ABS_TOL+RGB_REL_TOL*abs(a) for a,b in zip(correct,wrong)),
                            (c['pair'],c['label']))

    def test_hull_ag_normal_cube_and_point_witnesses(self):
        cases=fixture_cases()
        for p in (*range(70,76),*range(80,86),*range(100,106)):
            alpha=next(c for c in cases if c['pair']==p and c['label']=='hull_normal_alpha_binormal')
            unused=next(c for c in cases if c['pair']==p and c['label']=='hull_normal_unused_red_blue')
            self.assertEqual(expected(alpha),expected(unused))
            a,b=[c for c in cases if c['pair']==p and c['label']=='hull_cube_direction']
            self.assertNotEqual(expected(a),expected(b))
            a,b=[c for c in cases if c['pair']==p and c['label']=='hull_geometric_point']
            self.assertEqual(expected(a),expected(b))


class AsteroidFixtureTests(unittest.TestCase):
    def test_previous_2498_payloads_are_exact(self):
        self.assertEqual(hashlib.sha256(binary_cases(fixture_cases()[:2498])).hexdigest(),
                         '314c78fef10423661c2c9927f03f13a48a242b88421d5ce7ced234658621b7c2')

    def test_complete_six_pair_depth_winding_gain_and_light_coverage(self):
        cases=fixture_cases()
        self.assertEqual({(c['pair'],c['depth'],c['reverse']) for c in cases if c['label']=='asteroid_pair_depth_face'},
                         {(p,d,r) for p in range(110,116) for d in (0,1) for r in (0,1)})
        self.assertEqual({c['pair'] for c in cases if c['label']=='asteroid_missing_history'},set(range(110,116)))
        for p in range(110,116):
            lights={c['lights'] for c in cases if c['pair']==p and c['label']=='asteroid_lights_gains'}
            self.assertEqual(lights,{1} if p in (112,115) else {0,1,8})
            self.assertEqual(PAIRS[p][0] in FIXED_VERTICES,p in (112,115))
            self.assertEqual({tuple(c['gains']) for c in cases if c['pair']==p and c['label']=='asteroid_independent_gains'},
                             {(0.,0.,0.),(4.,1.,1.),(1.,16.,1.),(1.,1.,16.)})
        self.assertEqual({PAIRS[c['pair']][1] for c in cases if c['label']=='asteroid_unit_diffuse'},set(ref.ASTEROID_PROFILES))

    def test_separate_color_decode_and_noncomplementary_weights_discriminate(self):
        for p in range(110,116):
            c=next(c for c in fixture_cases() if c['pair']==p and c['label']=='asteroid_weights' and c['coefficients'][:2]==[2.,.5])
            c=dict(c,point=[0.]*3,material=[1.]*3,dir0=[0.]*3,dir1=[0.]*3,fp16=0)
            result=expected(c)
            correct=[2*b**2.2+.5*d**2.2 for b,d in zip(c['diffuse'][:3],c['lightmap'][:3])]
            self.assertEqual(result.linear_rgb,tuple(correct))
            wrong_sum=[(2*b+.5*d)**2.2 for b,d in zip(c['diffuse'][:3],c['lightmap'][:3])]
            wrong_complement=[.5*b**2.2+.5*d**2.2 for b,d in zip(c['diffuse'][:3],c['lightmap'][:3])]
            for wrong in (wrong_sum,wrong_complement):
                self.assertTrue(any(abs(a-b)>RGB_ABS_TOL+RGB_REL_TOL*abs(a) for a,b in zip(result.encoded_rgba[:3],map(ref.encode,wrong))))

    def test_cubic_specular_and_no_outer_three_independent_lobe(self):
        for c in fixture_cases():
            if c['label']!='asteroid_cubic_specular':continue
            correct=expected(c).linear_rgb
            albedo=[b**2.2+.5*d**2.2 for b,d in zip(c['diffuse'][:3],c['lightmap'][:3])]
            for got,a in zip(correct,albedo):self.assertAlmostEqual(got,a*(1+.8**3),places=6)
            for lobe in (1+3*.8**3,1+.8**5,.4+.8**3,1+.8**10):
                self.assertTrue(any(abs(a-ref.encode(b*lobe))>RGB_ABS_TOL+RGB_REL_TOL*abs(a)
                                    for a,b in zip(expected(c).encoded_rgba[:3],albedo)))

    def test_second_direction_and_native_grazing_factor(self):
        for c in fixture_cases():
            if c['label']=='asteroid_second_direction':
                self.assertEqual(any(expected(c).linear_rgb),c['pair'] in (110,113))
            if c['label']=='asteroid_grazing_specular':
                albedo=[b**2.2+.5*d**2.2 for b,d in zip(c['diffuse'][:3],c['lightmap'][:3])]
                for got,a in zip(expected(c).linear_rgb,albedo):self.assertAlmostEqual(got,.8*a,places=6)
                for wrong in (1.2,2.0):
                    self.assertTrue(any(abs(a-ref.encode(b*wrong))>RGB_ABS_TOL+RGB_REL_TOL*abs(a)
                        for a,b in zip(expected(c).encoded_rgba[:3],albedo)))

    def test_detail_uv_has_independent_axes_and_times_three_discriminator(self):
        cases=[c for c in fixture_cases() if c['pair']==110 and c['label']=='asteroid_detail_uv']
        self.assertEqual([detail_sample(c) for c in cases],
                         [(.125,.375,.1875,.875),(.375,.125,.1875,.875),(.5,.375,.375,.875)])
        # Sampling at base UV would hit (0,0), (0,0), (1,0), respectively.
        for c,wrong in zip(cases,[(.125,.125,.0625,.875)]*2+[(.25,.125,.125,.875)]):
            altered=dict(c,flags=0,lightmap=list(wrong))
            self.assertNotEqual(expected(c),expected(altered))
        self.assertEqual({c['pair'] for c in fixture_cases() if c['label']=='asteroid_detail_uv'},set(range(110,116)))

    def test_bumped_directional_vs_geometric_point_and_alpha(self):
        cases=fixture_cases()
        for p in range(113,116):
            choose=lambda label:next(c for c in cases if c['pair']==p and c['label']==label)
            a=choose('asteroid_normal_alpha_binormal');b=choose('asteroid_normal_unused_red_blue')
            self.assertEqual(expected(a),expected(b))
            self.assertNotEqual(expected(a),expected(choose('asteroid_normal_green_tangent')))
            a,b=[c for c in cases if c['pair']==p and c['label']=='asteroid_geometric_point']
            self.assertEqual(expected(a),expected(b))
        for p in range(110,116):
            original=next(c for c in cases if c['pair']==p and c['label']=='asteroid_pair_depth_face')
            modified=dict(original,reverse=1,glow=1.,lightmap=[*original['lightmap'][:3],0.],gains=[16.]*3)
            self.assertEqual(expected_alpha(original),.46875)
            self.assertEqual(expected_alpha(original),expected_alpha(modified))
            self.assertEqual(expected(original).encoded_rgba[3],expected(modified).encoded_rgba[3])


if __name__ == '__main__':unittest.main()
