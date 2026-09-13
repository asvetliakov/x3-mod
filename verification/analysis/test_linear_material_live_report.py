"""Negative witnesses for the consume-only live GPU evidence checker."""
import copy
import json
from pathlib import Path
import re
import unittest
from run_linear_material_live import compare_cases, validate_case, ELIGIBLE, PIXEL_PROGRAMS, VERTEX_PROGRAMS, BUMP_FRAMES, MATCHED, FRAME_COUNT, CORPUS_PAIRS, IMPLEMENTED_PROGRAMS, MOTION_FRAMES, UNKNOWN_FRAMES, UNKNOWN_PIXEL, ASTEROID_FRAMES, expected_rgb


def cases():
    result = {}
    for owner in (0, 1):
        for taa in (0, 1):
            for material in (0, 1):
                result[f'ownership{owner}-taa{taa}-material{material}'] = {
                    'temporal_hashes': [['motion', 'depth']] * FRAME_COUNT,
                    'native_hashes': ['native'] * FRAME_COUNT,
                    'held_references': 20 + material * 83,
                    'pixel_programs': list(PIXEL_PROGRAMS),
                    'vertex_programs': list(VERTEX_PROGRAMS),
                    'rgba': [expected_rgb(frame,material) + [.75] for frame in range(FRAME_COUNT)],
                }
    return result


def sample_report():
    output = [f'RESULT PASS checks=1 restorations=1 frames={FRAME_COUNT} depth_written={FRAME_COUNT} taa_reference_frames={FRAME_COUNT} motion_pixels={FRAME_COUNT} matched_pixels={len(MATCHED)}']
    trace = [f'linear_material_variant kind={stage} original={identifier} transform=0 create=00000000'
             for stage, identifier in sorted(IMPLEMENTED_PROGRAMS)]
    trace += ['motion_output_release held=69 released=1']
    for frame in range(FRAME_COUNT):
        eligible = int(frame in ELIGIBLE)
        output += [f'LINEAR_LIVE frame={frame} combined={eligible} refusal={1 if frame in (9,19) else 4 if frame in MOTION_FRAMES-ELIGIBLE else 0} vs={VERTEX_PROGRAMS[frame]} ps={PIXEL_PROGRAMS[frame]} rgba=1,1,1,0.75 native_hash=abcdef',
                   f'MOTION_HASH frame={frame} motion=1234 depth=5678']
        trace += [f'motion_output_frame frame={frame} routed={int(frame in MOTION_FRAMES)} depth_routed={int(frame in MOTION_FRAMES)} matched={int(frame in MATCHED)} gate3={int(frame in UNKNOWN_FRAMES)} apply_failures=0 restore_failures=0 taa_resolved=1',
                  f'linear_material_frame frame={frame} routed={eligible} refused={int(frame in MOTION_FRAMES and not eligible)} bind_failures=0 bump_routed={int(eligible and frame in BUMP_FRAMES)}']
    return '\n'.join(output), trace


class LiveMaterialReportTests(unittest.TestCase):
    def test_valid_control_twins(self):
        compare_cases(cases())
        output, trace = sample_report()
        self.assertEqual(validate_case(output, trace, True, True)['pixel_programs'], PIXEL_PROGRAMS)

    def test_complete_pair_inventory_and_history_schedule(self):
        self.assertEqual(len(CORPUS_PAIRS), 116)
        self.assertEqual(len({(v,p) for v,p,_ in CORPUS_PAIRS}), 116)
        self.assertEqual(len(IMPLEMENTED_PROGRAMS), 83)
        self.assertEqual(FRAME_COUNT, 258)
        for index, (vertex,pixel,bump) in enumerate(CORPUS_PAIRS):
            for repeat in range(2):
                frame = 24 + index*2 + repeat
                self.assertEqual((VERTEX_PROGRAMS[frame],PIXEL_PROGRAMS[frame]), (vertex,pixel))
                self.assertEqual(frame in MATCHED, bool(repeat))
                self.assertEqual(frame in BUMP_FRAMES, bump)
        changed = cases()
        changed['ownership0-taa1-material1']['pixel_programs'][-1] = 'ef2bf556f207b8bd'
        with self.assertRaises(AssertionError): compare_cases(changed)
        output, trace = sample_report()
        trace = [line.replace('matched=1','matched=0') if line.startswith('motion_output_frame frame=243 ') else line for line in trace]
        with self.assertRaises(AssertionError): validate_case(output, trace, True, True)

    def test_cpp_corpus_matches_runner_and_proved_shapes(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'verification/probe/motion_output_fixture.cpp').read_text()
        source = source[source.index('    void run_linear_materials('):source.index('    // ---- FP16 HDR scene path')]
        vertices = re.findall(r'"([0-9a-f]{16})"', re.search(r'corpus_vs\[\]=\{([^}]+)\}', source).group(1))
        pixels = re.findall(r'"([0-9a-f]{16})"', re.search(r'corpus_ps\[\]=\{([^}]+)\}', source).group(1))
        table = re.search(r'const CorpusPair corpus\[\]=\{(.*?)\n        \};', source, re.S).group(1)
        rows = re.findall(r'\{(\d+),(\d+),(true|false),(true|false),(true|false),(true|false)\}', table)
        self.assertEqual(len(rows),116)
        report = json.loads((root / 'docs/reverse-engineering/linear-material-profiles.json').read_text())
        programs = {p['id']:p for p in report['programs']}
        for index,(v,p,bump,affine,standard,low) in enumerate(rows):
            vertex,pixel = vertices[int(v)],pixels[int(p)]
            self.assertEqual((vertex,pixel,bump=='true'),CORPUS_PAIRS[index])
            profile = programs['ps_'+pixel]
            self.assertEqual(affine=='true',bool(profile['diffuse_affine_completion']))
            self.assertEqual(standard=='true',profile['families'][0].startswith('standard'))
            self.assertEqual(low=='true',profile['families']==['standard_bump_low'])
        self.assertEqual(set(IMPLEMENTED_PROGRAMS),{tuple(p['id'].split('_',1)) for p in report['programs']})

    def test_valid_xt_and_unknown_controls_have_distinct_routes(self):
        for frame in (9,19):
            self.assertEqual((VERTEX_PROGRAMS[frame],PIXEL_PROGRAMS[frame]), ('37c34a7478544c14','5f82ecacd39529cd'))
            self.assertIn(frame,MOTION_FRAMES)
            self.assertNotIn(frame,ELIGIBLE)
        for frame in UNKNOWN_FRAMES:
            self.assertEqual((VERTEX_PROGRAMS[frame],PIXEL_PROGRAMS[frame]),('53a0a641107ed76c',UNKNOWN_PIXEL))
            self.assertNotIn(frame,MOTION_FRAMES|ELIGIBLE|MATCHED)
        controls = cases()
        for case in controls.values(): case['rgba'][19][0] = float('nan')
        with self.assertRaises(AssertionError): compare_cases(controls)
        output,trace=sample_report()
        report=validate_case(output,trace,True,True)
        self.assertEqual(report['refused_frames'],[2,3,4,5,7,9,13,15,19])
        self.assertTrue(UNKNOWN_FRAMES.isdisjoint(report['refused_frames']))
        for old,new in [('routed=0 depth_routed=0','routed=1 depth_routed=1'),('matched=0','matched=1')]:
            changed=[line.replace(old,new) if line.startswith('motion_output_frame frame=256 ') else line for line in trace]
            with self.assertRaises(AssertionError): validate_case(output,changed,True,True)
        changed=cases();changed['ownership0-taa1-material1']['native_hashes'][256]='changed'
        with self.assertRaises(AssertionError): compare_cases(changed)

    def test_stdout_refusal_reason_schedule_is_checked(self):
        output,trace=sample_report()
        for frame,wrong in ((256,1),(257,1),(9,0),(19,4),(2,1),(7,0),(13,0),(15,1),(0,4),(244,1)):
            changed='\n'.join(re.sub(r'refusal=\d+',f'refusal={wrong}',line) if line.startswith(f'LINEAR_LIVE frame={frame} ') else line for line in output.splitlines())
            with self.subTest(frame=frame),self.assertRaises(AssertionError):
                validate_case(changed,trace,True,True)

    def test_unknown_pixel_has_only_supplied_color0_input(self):
        import struct
        source=(Path(__file__).resolve().parents[2]/'verification/probe/motion_output_fixture.cpp').read_text()
        text=re.search(r'const DWORD unknown_program\[\]=\{([^}]+)\}',source).group(1)
        words=[int(token.strip().rstrip('u'),16) for token in text.split(',')]
        value=14695981039346656037
        for byte in struct.pack('<%dI'%len(words),*words): value=((value^byte)*1099511628211)&((1<<64)-1)
        self.assertEqual(f'{value:016x}',UNKNOWN_PIXEL)
        self.assertEqual(words[7:10],[0x0200001f,0x8000000a,0x900f0000])
        self.assertEqual(words[10:],[0x02000001,0x800f0800,0xa0e40000,0x03000005,0x80080800,0xa0ff0000,0x90ff0000,0xffff])

    def test_asteroid_expected_colors_and_second_history_are_required(self):
        self.assertEqual(ASTEROID_FRAMES,set(range(244,256)))
        for frame in ASTEROID_FRAMES:
            self.assertEqual(frame in MATCHED,bool((frame-244)%2))
            self.assertNotEqual(expected_rgb(frame,True),expected_rgb(24,True))
        changed=cases();changed['ownership1-taa0-material1']['rgba'][244][1]=1.
        with self.assertRaises(AssertionError): compare_cases(changed)
        output,trace=sample_report()
        changed=[line.replace('matched=1','matched=0') if line.startswith('motion_output_frame frame=255 ') else line for line in trace]
        with self.assertRaises(AssertionError): validate_case(output,changed,True,True)

    def test_rejects_lost_temporal_color_alpha_or_references(self):
        base = cases()
        for failure in ('temporal', 'refcount', 'fallback', 'alpha', 'activation', 'shared_activation', 'shared_schedule', 'bump_activation', 'bump_refusal', 'bump_schedule', 'bump_reset', 'reset'):
            changed = copy.deepcopy(base)
            item = changed['ownership0-taa1-material1']
            if failure == 'temporal': item['temporal_hashes'][3] = ['wrong', 'depth']
            elif failure == 'refcount': item['held_references'] -= 1
            elif failure == 'fallback': item['native_hashes'][9] = 'changed'
            elif failure == 'alpha': item['rgba'][0][3] += .1
            elif failure == 'activation': item['rgba'][0][0] = 1.
            elif failure == 'shared_activation': item['rgba'][1][0] = 1.
            elif failure == 'shared_schedule': item['pixel_programs'][1] = '462342e3e5781384'
            elif failure == 'bump_activation': item['rgba'][12][0] = 1.
            elif failure == 'bump_refusal': item['rgba'][13][0] += .1
            elif failure == 'bump_schedule': item['vertex_programs'][12] = '53a0a641107ed76c'
            elif failure == 'bump_reset': item['rgba'][21][0] = 1.
            else: item['rgba'][10][0] = 1.
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                compare_cases(changed)
        output, trace = sample_report()
        for failure in ('missing_shared_variant', 'split_variant', 'split_admitted', 'bump_counter', 'bump_negative_admitted', 'missing_bump_variant'):
            changed = list(trace)
            if failure == 'missing_shared_variant': del changed[2]
            elif failure == 'split_variant': changed[2] = 'linear_material_variant kind=ps original=ef2bf556f207b8bd transform=0 create=00000000'
            elif failure == 'split_admitted': changed = [line.replace('routed=0 refused=1', 'routed=1 refused=0') if line.startswith('linear_material_frame frame=9 ') else line for line in changed]
            elif failure == 'bump_counter': changed = [line.replace('bump_routed=1', 'bump_routed=0') if line.startswith('linear_material_frame frame=12 ') else line for line in changed]
            elif failure == 'bump_negative_admitted': changed = [line.replace('routed=0 refused=1', 'routed=1 refused=0') if line.startswith('linear_material_frame frame=19 ') else line for line in changed]
            else: del changed[4]
            with self.subTest(failure=failure), self.assertRaises(AssertionError):
                validate_case(output, changed, True, True)


if __name__ == '__main__':
    unittest.main()
