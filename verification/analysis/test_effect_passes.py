"""Synthetic compiled-effect containers; no archive bytes are retained here.

Each blob is assembled from the documented D3DXFX layout by `Effect` below and
covers one classification class of `tools/analysis/effect_passes.py`: constant
render states, constant sampler states, texture bindings, shader bindings and
the parameter-driven forms (parameter reference, array selector, FXLC
expression).
"""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/analysis'))
from effect_passes import (MalformedEffect, PASS_CLASSES, STATE_TABLE,  # noqa: E402
                           classify_effect, parse_effect)

MAGIC = b'\x01\x09\xff\xfe'
VERTEX_SHADER, PIXEL_SHADER = 146, 147
Z_ENABLE, ALPHA_REF, COLOR_OP = 0, 10, 103
TEXTURE, ADDRESS_U, MIN_FILTER = 164, 165, 170


def _pad(payload):
    return payload + b'\0' * (-len(payload) % 4)


def shader(version, samplers=()):
    """A minimal SM stream: a CTAB comment with `samplers` plus the end token."""
    types = 28 + 20 * len(samplers)
    table = bytearray(struct.pack('<7I', 28, 0, version, len(samplers), 28, 0, 0))
    table += b'\0' * (20 * len(samplers))
    table += b'\0' * 16  # one shared type record: class 0, type 0, no members
    names = bytearray()
    for index, name in enumerate(samplers):
        struct.pack_into('<I4H2I', table, 28 + 20 * index, types + 16 + len(names),
                         3, index, 1, 0, types, 0)
        names += name.encode() + b'\0'
    table = _pad(bytes(table) + bytes(names))
    comment = struct.pack('<I', 0xfffe | ((1 + len(table) // 4) << 16)) + b'CTAB' + table
    return struct.pack('<I', version) + comment + struct.pack('<I', 0x0000ffff)


class Effect:
    """Assemble one compiled effect container from parameter and pass records."""

    def __init__(self):
        self.pool = bytearray()
        self.parameters, self.techniques, self.resources = [], [], []

    def _add(self, payload):
        offset = len(self.pool)
        self.pool += _pad(payload)
        return offset

    def name(self, text):
        return self._add(struct.pack('<I', len(text) + 1) + text.encode() + b'\0')

    def typedef(self, type_id, class_id=0, name='', elements=0):
        return self._add(struct.pack('<5I', type_id, class_id,
                                     self.name(name) if name else 0, 0, elements))

    def value(self, *words):
        return self._add(struct.pack('<%dI' % len(words), *words))

    def states(self, records, elements=1):
        """A state-list value: `(operation, typedef type)` records per element."""
        words = []
        for _ in range(elements):
            words.append(len(records))
            for operation, type_id in records:
                words += [operation, 0, self.typedef(type_id), self.value(0)]
        return self._add(struct.pack('<%dI' % len(words), *words))

    def parameter(self, type_id, name, value_offset, elements=0):
        index = len(self.parameters)
        self.parameters.append((self.typedef(type_id, 4, name, elements), value_offset, 0))
        return index

    def technique(self, name, passes):
        self.techniques.append((self.name(name), passes))

    def resource(self, technique, index, element, state, usage, payload):
        self.resources.append((technique, index, element, state, usage, payload))

    def build(self):
        body = bytearray(struct.pack('<4I', len(self.parameters), len(self.techniques), 0, 0))
        for typedef_offset, value_offset, flags in self.parameters:
            body += struct.pack('<4I', typedef_offset, value_offset, flags, 0)
        for name_offset, passes in self.techniques:
            body += struct.pack('<3I', name_offset, 0, len(passes))
            for pass_name, states in passes:
                body += struct.pack('<3I', self.name(pass_name), 0, len(states))
                for operation, index, type_id in states:
                    body += struct.pack('<4I', operation, index, self.typedef(type_id),
                                        self.value(0))
        body += struct.pack('<2I', 0, len(self.resources))
        for technique, index, element, state, usage, payload in self.resources:
            body += struct.pack('<6I', technique, index, element, state, usage, len(payload))
            body += _pad(payload)
        return MAGIC + struct.pack('<I', len(self.pool)) + bytes(self.pool) + bytes(body)


def one_pass(effect, states, **kwargs):
    effect.technique('T', [('P', states)])
    return classify_effect(effect.build(), **kwargs)[0]


class StateTableTests(unittest.TestCase):
    def test_anchors_hold(self):
        for operation, name in ((48, 'Lighting'), (99, 'SeparateAlphaBlendEnable'),
                                (VERTEX_SHADER, 'VertexShader'), (PIXEL_SHADER, 'PixelShader'),
                                (TEXTURE, 'Texture'), (174, 'MaxAnisotropy')):
            self.assertEqual(STATE_TABLE[operation][0], name)

    def test_categories(self):
        self.assertEqual(STATE_TABLE[Z_ENABLE][1], 'render')
        self.assertEqual(STATE_TABLE[MIN_FILTER][1], 'sampler_state')
        self.assertEqual(STATE_TABLE[COLOR_OP][1], 'texture_stage')


class RenderStateTests(unittest.TestCase):
    def test_literal_render_state_is_constant(self):
        record = one_pass(Effect(), [(Z_ENABLE, 0, 1)])
        self.assertEqual(record['counts']['render_const'], 1)
        self.assertEqual(record['constant'], 1)
        self.assertEqual(record['constant_fraction'], 1.0)
        self.assertTrue(record['replayable'])

    def test_expression_parameter_and_selector_are_parameter_driven(self):
        effect = Effect()
        effect.resource(0, 0, 0xffffffff, 0, 0, b'FXLC')          # expression
        effect.resource(0, 0, 0xffffffff, 1, 1, b'g_AlphaRef\0')  # parameter reference
        effect.resource(0, 0, 0xffffffff, 2, 2, b'g_Index\0')     # array selector
        record = one_pass(effect, [(Z_ENABLE, 0, 1), (ALPHA_REF, 0, 2), (ALPHA_REF, 0, 2)],
                          detail=True)
        self.assertEqual(record['counts']['render_param'], 3)
        self.assertEqual(record['constant'], 0)
        self.assertEqual(record['parameter_driven'], 3)
        self.assertEqual([entry['kind'] for entry in record['entries']],
                         ['expression', 'parameter', 'array_selector'])
        self.assertEqual(record['entries'][1]['source'], 'g_AlphaRef')
        self.assertFalse(record['replayable'])  # the expression excludes the pass

    def test_parameter_reference_alone_stays_replayable(self):
        effect = Effect()
        effect.resource(0, 0, 0xffffffff, 0, 1, b'g_ZEnable\0')
        self.assertTrue(one_pass(effect, [(Z_ENABLE, 0, 1)])['replayable'])


class ShaderBindingTests(unittest.TestCase):
    def test_blob_and_null_shader_are_constant_bindings(self):
        effect = Effect()
        effect.resource(0, 0, 0xffffffff, 0, 0, shader(0xfffe0101))
        record = one_pass(effect, [(VERTEX_SHADER, 0, 16), (PIXEL_SHADER, 0, 15)], detail=True)
        self.assertEqual(record['counts']['shader'], 2)
        self.assertEqual(record['kinds'], {'shader_blob': 1, 'null_shader': 1,
                                           'unresolved_sampler': 0})
        self.assertEqual(record['parameter_driven'], 0)
        self.assertTrue(record['replayable'])

    def test_pass_shader_pairing_still_parses(self):
        effect = Effect()
        effect.resource(0, 0, 0xffffffff, 0, 0, shader(0xfffe0101))
        effect.technique('T', [('P', [(VERTEX_SHADER, 0, 16), (PIXEL_SHADER, 0, 15)])])
        parsed = parse_effect(effect.build())
        self.assertEqual(parsed[0]['technique'], 'T')
        self.assertEqual([state[1] for state in parsed[0]['states']], ['vs', 'ps'])
        self.assertEqual(parsed[0]['states'][0][4], 'program')
        self.assertEqual(parsed[0]['states'][1][4], 'null')


class SamplerBlockTests(unittest.TestCase):
    def build(self, samplers=('S',)):
        effect = Effect()
        block = effect.states([(TEXTURE, 5), (MIN_FILTER, 2), (ADDRESS_U, 2)])
        index = effect.parameter(12, 'S', block)
        effect.resource(0xffffffff, index, 0, 0, 1, b't_Diffuse\0')  # Texture = <parameter>
        effect.resource(0xffffffff, index, 0, 2, 0, b'FXLC')         # AddressU = expression
        effect.resource(0, 0, 0xffffffff, 0, 0, shader(0xffff0200, samplers))
        return effect

    def test_sampler_states_and_texture_binding_are_attributed_to_the_pass(self):
        record = one_pass(self.build(), [(VERTEX_SHADER, 0, 16)], detail=True)
        self.assertEqual(record['samplers'], 1)
        self.assertEqual(record['counts']['texture_param'], 1)
        self.assertEqual(record['counts']['sampler_const'], 1)
        self.assertEqual(record['counts']['sampler_param'], 1)
        self.assertEqual(record['counts']['shader'], 1)
        self.assertEqual(record['states'], 4)
        self.assertEqual(record['constant'], 2)
        self.assertEqual([entry['scope'] for entry in record['entries'][1:]], ['sampler:S[0]'] * 3)
        self.assertEqual(record['entries'][1]['source'], 't_Diffuse')

    def test_sampler_the_shaders_do_not_bind_is_not_counted(self):
        record = one_pass(self.build(samplers=()), [(VERTEX_SHADER, 0, 16)])
        self.assertEqual(record['samplers'], 0)
        self.assertEqual(record['states'], 1)

    def test_unknown_sampler_name_counts_as_unresolved(self):
        effect = Effect()
        effect.resource(0, 0, 0xffffffff, 0, 0, shader(0xffff0200, ('Absent',)))
        record = one_pass(effect, [(VERTEX_SHADER, 0, 16)])
        self.assertEqual(record['counts']['unknown'], 1)
        self.assertFalse(record['replayable'])

    def test_sampler_array_elements_are_walked(self):
        effect = Effect()
        block = effect.states([(MIN_FILTER, 2)], elements=2)
        effect.parameter(12, 'S', block, elements=2)
        effect.resource(0, 0, 0xffffffff, 0, 0, shader(0xffff0200, ('S',)))
        record = one_pass(effect, [(VERTEX_SHADER, 0, 16)], detail=True)
        self.assertEqual(record['counts']['sampler_const'], 2)
        self.assertEqual([entry['scope'] for entry in record['entries'][1:]],
                         ['sampler:S[0]', 'sampler:S[1]'])


class OtherStateTests(unittest.TestCase):
    def test_texture_stage_state_is_other_and_blocks_replay(self):
        record = one_pass(Effect(), [(COLOR_OP, 0, 2)])
        self.assertEqual(record['counts']['other_const'], 1)
        self.assertFalse(record['replayable'])

    def test_unknown_operation_is_counted_apart(self):
        record = one_pass(Effect(), [(9999, 0, 2)], detail=True)
        self.assertEqual(record['counts']['unknown'], 1)
        self.assertEqual(record['entries'][0]['name'], 'op_9999')
        self.assertFalse(record['replayable'])

    def test_counts_cover_every_reported_class(self):
        record = one_pass(Effect(), [(Z_ENABLE, 0, 1)])
        self.assertEqual(sorted(record['counts']), sorted(PASS_CLASSES))
        self.assertEqual(sum(record['counts'].values()), record['states'])


class MalformedTests(unittest.TestCase):
    def test_trailing_bytes_are_rejected(self):
        effect = Effect()
        effect.technique('T', [('P', [(Z_ENABLE, 0, 1)])])
        with self.assertRaises(MalformedEffect):
            classify_effect(effect.build() + b'\0\0\0\0')

    def test_magic_is_checked(self):
        with self.assertRaises(MalformedEffect):
            classify_effect(b'\0' * 32)


if __name__ == '__main__':
    unittest.main()
