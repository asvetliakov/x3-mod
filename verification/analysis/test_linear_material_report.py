"""Reject missing/duplicate/nonfinite GPU evidence and independently serialize cases."""
import struct
import unittest
from run_linear_material import PAIRS, fixture_cases, binary_cases, expected, validate_report


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
    for lights in (0, 8):
        for i in range(18):
            mode = 2-i%3 if (i//3)%2 else i%3
            lines.append(f'TIMING lights={lights} mode={mode} iteration={i} draws=4 vertices=98304 width=256 completed_ms=1.5')
    lines.append(f'RESULT PASS cases={len(cases)}')
    return '\n'.join(lines)


class ReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = report()

    def test_complete_report(self):
        result = validate_report(self.text)
        self.assertEqual(result['pairs'], 10)
        self.assertGreater(result['hdr_channels'], 0)
        self.assertGreater(result['exact_black_channels'], 0)

    def test_inventory_and_binary_abi(self):
        cases = fixture_cases()
        data = binary_cases(cases)
        self.assertEqual(len(data), 4 + 160*len(cases))
        self.assertEqual(struct.unpack_from('<I', data)[0], len(cases))
        self.assertEqual(set((c['pair'],c['depth'],c['reverse']) for c in cases if c['label']=='pair_depth_face'),
                         {(p,d,r) for p in range(10) for d in (0,1) for r in (0,1)})
        self.assertEqual({c['lights'] for c in cases if c['label']=='lights_gains'}, {0,1,8})
        self.assertEqual(len([c for c in cases if c['label'].startswith('safety_')]), 54)

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
