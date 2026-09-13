"""Bounded damage token proof; local copyrighted shader inputs stay untracked."""
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import inspect_motion_output_profiles as profiles

PROGRAMS = Path('/tmp/x3-shader-sweep/programs')


class DamageContractTests(unittest.TestCase):
    def native(self, identifier):
        path = PROGRAMS / f'ps_{identifier}.bin'
        if not path.exists():
            self.skipTest('local shader sweep unavailable')
        code = path.read_bytes()
        self.assertEqual(profiles.fnv1a64(code), identifier)
        return code

    def proof(self, words, identifier, header):
        try:
            parsed, items, _ = profiles.instructions(struct.pack(f'<{len(words)}I', *words))
        except profiles.Malformed:
            return False
        return profiles.damage_flow_proof(parsed, items, identifier, header)['qualified']

    def test_every_critical_token_bit_and_forbidden_flow(self):
        for identifier, contract in profiles.DAMAGE_CONTRACTS.items():
            code = self.native(identifier)
            words, items, _ = profiles.instructions(code)
            digest = profiles.profile(code, identifier, 'ps', '3_0')
            header = digest['header_end_dword']
            self.assertTrue(self.proof(words, identifier, header))
            flow = contract['flow']
            critical = (flow + [flow[0] + 1, flow[2] + 1, flow[2] + 2, flow[4] + 1]
                        + list(range(flow[2] + 3, flow[2] + 6))
                        + list(range(contract['mad'], contract['mad'] + 5))
                        + list(range(contract['cmp'], contract['cmp'] + 5))
                        + [contract['rgb'], contract['rgb'] + 1, contract['rgb'] + 5, contract['rgb'] + 6]
                        + list(range(1313, 1319)))
            self.assertEqual(len(set(critical)), 34)
            for at in critical:
                for bit in range(32):
                    mutant = list(words)
                    mutant[at] ^= 1 << bit
                    with self.subTest(identifier=identifier, at=at, bit=bit):
                        self.assertFalse(self.proof(mutant, identifier, header))
            for opcode in [25, 26, 27, 28, 29, 30, 38, 39, 40, 41, 42, 43, 44, 45, 65, 94, 96]:
                mutant = list(words)
                mutant[header] = mutant[header] & ~0xffff | opcode
                self.assertFalse(self.proof(mutant, identifier, header))
            for bit in [profiles.PREDICATED, profiles.COISSUE]:
                mutant = list(words)
                mutant[header] |= bit
                self.assertFalse(self.proof(mutant, identifier, header))
            self.assertFalse(self.proof(words, '0000000000000000', header))

    def test_only_owned_pair_gets_the_dynamic_class(self):
        vertex_path = PROGRAMS / f'vs_{profiles.DAMAGE_VERTEX}.bin'
        if not vertex_path.exists():
            self.skipTest('local shader sweep unavailable')
        vertex = profiles.profile(vertex_path.read_bytes(), profiles.DAMAGE_VERTEX, 'vs', '3_0')
        for identifier in profiles.DAMAGE_CONTRACTS:
            pixel = profiles.profile(self.native(identifier), identifier, 'ps', '3_0')
            self.assertEqual(profiles.classify(vertex, pixel)[0], 'D_bounded_damage_branches')
            foreign = dict(vertex, fnv1a64='0000000000000000')
            self.assertEqual(profiles.classify(foreign, pixel)[0], 'X_unsupported')
            unproved = dict(pixel, bounded_damage_flow={'qualified': False})
            self.assertEqual(profiles.classify(vertex, unproved)[0], 'X_unsupported')


if __name__ == '__main__':
    unittest.main()
