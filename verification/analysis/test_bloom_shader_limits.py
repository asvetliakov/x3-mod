"""Boundary controls for the authored-bloom SM3 static resource gate."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
from bloom_shader_limits import check_profile


def sample():
    return dict(parsed=True, version='0xffff0300', opcode_counts={'mov': 506, 'lrp': 2, 'texldl': 1},
        relative_addressing={'present': False}, predicated_or_coissued_dwords=[],
        control_flow_balanced=True, control_flow_max_depth=0,
        declared_samplers=[{'register': 0, 'texture_type': 2}], temporary_registers=[0],
        sampler_registers=[0], constant_registers_direct=[28], defined_constant_registers=[0],
        executable_instruction_count=509)


class BloomShaderLimits(unittest.TestCase):
    def test_two_slot_texldl_and_lrp_at_minimum_budget(self):
        p = sample()
        self.assertEqual(check_profile(p)['instruction_slots'], 512)
        # Counting a non-cube TEXLDL as one slot would incorrectly admit this.
        p['opcode_counts']['mov'] += 1
        with self.assertRaises(ValueError):
            check_profile(p)

    def test_sparse_high_registers_are_not_just_counts(self):
        for field, value in [('temporary_registers', [32]), ('sampler_registers', [16]),
                             ('constant_registers_direct', [224]), ('defined_constant_registers', [224])]:
            p = sample(); p[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check_profile(p)

    def test_unknown_texture_form_and_execution_shape_refused(self):
        for mutate in [lambda p: p['opcode_counts'].update(call=1),
                       lambda p: p['opcode_counts'].update(ifc=1),
                       lambda p: p['opcode_counts'].update(setp=1),
                       lambda p: p['declared_samplers'][0].update(texture_type=3),
                       lambda p: p['relative_addressing'].update(present=True),
                       lambda p: p.update(predicated_or_coissued_dwords=[1]),
                       lambda p: p.update(control_flow_balanced=False),
                       lambda p: p.update(control_flow_max_depth=25)]:
            p = sample(); mutate(p)
            with self.assertRaises(ValueError):
                check_profile(p)


if __name__ == '__main__':
    unittest.main()
