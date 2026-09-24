"""Shader slot budget record (run_shader_slot_budget.py): strict JSON, caps, and the no-refusal facts."""
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / 'verification/results/bottle-X3/shader-slot-budget.json'


def strict(name):
    raise ValueError(f'non-standard JSON constant {name}')


class ShaderSlotBudgetRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.record = json.loads(RECORD.read_text(), parse_constant=strict)
        cls.cases = cls.record['report']['cases']

    def case(self, stage, kind, n, loop=0):
        rows = [c for c in self.cases if (c['stage'], c['kind'], c['n'], c['loop']) == (stage, kind, n, loop)]
        self.assertEqual(len(rows), 1, (stage, kind, n, loop))
        return rows[0]

    def test_run_and_bottle(self):
        self.assertTrue(self.record['passed'])
        self.assertFalse(self.record['game_launched'])
        self.assertEqual(self.record['bottle']['name'], 'X3')
        self.assertEqual(self.record['bottle']['wine_arch'], 'arm64')

    def test_caps(self):
        caps = self.record['report']['caps']
        self.assertEqual(caps['hr'], '00000000')
        self.assertEqual((caps['ps_version'], caps['vs_version']), ('0300', '0300'))
        self.assertEqual((caps['max_ps30_slots'], caps['max_vs30_slots'], caps['ps20_slots']), (512, 512, 512))
        self.assertEqual((caps['max_ps_executed'], caps['max_vs_executed']), (65535, 65535))

    def test_no_refusal_above_the_cap(self):
        # D3DX compile, D3DX assemble and CreatePixelShader accept programs far above the reported 512.
        hlsl = self.case('ps_3_0', 'hlsl', 16384)
        self.assertEqual((hlsl['compile_hr'], hlsl['create_hr'], hlsl['slots']), ('00000000', '00000000', 16385))
        assembled = self.case('ps_3_0', 'asm', 32768)
        self.assertEqual((assembled['compile_hr'], assembled['create_hr'], assembled['slots']), ('00000000', '00000000', 32770))
        raw = self.case('ps_3_0', 'raw', 262144)
        self.assertEqual((raw['create_hr'], raw['slots']), ('00000000', 262146))
        self.assertEqual(self.case('vs_3_0', 'raw', 65536)['create_hr'], '00000000')
        refused = [c for c in self.cases if c.get('create_hr') not in (None, '-', '00000000')]
        self.assertEqual(refused, [])

    def test_executed_exactly(self):
        for stage, kind, n, loop in (('ps_3_0', 'hlsl', 513, 0), ('ps_3_0', 'hlsl', 16384, 0), ('ps_3_0', 'raw', 32768, 0),
                                     ('ps_3_0', 'hlsl', 10, 16), ('ps_3_0', 'hlsl', 500, 132), ('vs_3_0', 'hlsl', 4096, 0)):
            c = self.case(stage, kind, n, loop)
            self.assertEqual(c['draw_hr'], '00000000')
            self.assertEqual(c['executed'], c['executed_expected'], (stage, kind, n, loop))

    def test_rolled_loop_charged_once(self):
        loop = self.case('ps_3_0', 'hlsl', 10, 16)
        self.assertLess(loop['slots'], 32)
        self.assertEqual(self.case('ps_3_0', 'hlsl', 500, 64)['slots'], 508)


if __name__ == '__main__':
    unittest.main()
