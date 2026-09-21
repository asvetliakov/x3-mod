"""Schema-2 geometry bundle validation; synthetic bytes, no Wine or game."""
import copy
import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest

from lattice_state_packet import (GEOMETRY_BYTES, GEOMETRY_FORMAT, GEOMETRY_RANGES,
                                  GEOMETRY_SCOPE, load, validate)
from verification.analysis.test_lattice_state_capture import packet as state_packet


def geometry_bytes():
    # Zero indices are in range for both fixed slots; vertex attributes are
    # preserved opaque bytes and need no normalization by this reader.
    return bytes(GEOMETRY_BYTES)


def payload_packet(data=None):
    data = geometry_bytes() if data is None else data
    p = state_packet()
    p.update(schema=2, pid=123, payload_copy_valid=True)
    name = 'lattice-geometry-123-1-8-0.bin'
    p['geometry'] = {'format': GEOMETRY_FORMAT, 'scope': GEOMETRY_SCOPE,
                     'status': 'complete', 'file': name, 'bytes': GEOMETRY_BYTES,
                     'sha256': hashlib.sha256(data).hexdigest(), 'copy_ticks': 17}
    for slot, record in enumerate(p['records']):
        ranges = GEOMETRY_RANGES[slot]
        base = 0x20000000000001 + slot * 8
        record['upload'] = {
            'pair_status': 'copied', 'attachment_status': 'valid',
            'producer_payload_valid': True,
            'binding_revision_match_at_observation': True,
            'arm_serial': '0020000000000001', 'owner': '0020000000000002',
            'generation': '0020000000000003', 'invocation': f'{base:016x}',
            'vertex': {'allocation': f'{base + 1:016x}', 'revision': f'{base + 2:016x}',
                       'offset': ranges['vertex'][0], 'bytes': ranges['vertex'][1]},
            'index': {'allocation': f'{base + 3:016x}', 'revision': f'{base + 4:016x}',
                      'offset': ranges['index'][0], 'bytes': ranges['index'][1]},
        }
    return p


def refusal_packet():
    p = payload_packet()
    p['payload_copy_valid'] = False
    p['geometry'].update(status='unavailable', file=None, bytes=0, sha256=None)
    p['records'][0]['upload'] = {
        'pair_status': 'revision', 'attachment_status': 'copy_refused',
        'producer_payload_valid': False,
        'binding_revision_match_at_observation': False, 'invalidated_by': None}
    p['records'][1]['upload'] = {
        'pair_status': 'copied', 'attachment_status': 'sibling_refused',
        'producer_payload_valid': False,
        'binding_revision_match_at_observation': False, 'invalidated_by': None}
    return p


class LatticePayloadPacketTests(unittest.TestCase):
    def write_bundle(self, root, packet=None, data=None):
        data = geometry_bytes() if data is None else data
        packet = payload_packet(data) if packet is None else packet
        state = Path(root) / 'lattice-state-123-1-8-0.json'
        state.write_text(json.dumps(packet))
        if packet['geometry']['file'] is not None:
            (Path(root) / packet['geometry']['file']).write_bytes(data)
        return state

    def test_complete_bundle_and_state_payload_requirements_are_separate(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write_bundle(tmp)
            self.assertEqual(load(path, require_complete=True, require_payload=True)['schema'], 2)
            refusal = refusal_packet()
            path.write_text(json.dumps(refusal))
            self.assertEqual(load(path, require_complete=True), refusal)
            with self.assertRaisesRegex(ValueError, 'payload required'):
                load(path, require_payload=True)
            schema1 = state_packet()
            path.write_text(json.dumps(schema1))
            with self.assertRaisesRegex(ValueError, 'state-only'):
                load(path, require_payload=True)

    def test_exact_ranges_identities_and_refusal_envelopes(self):
        mutations = []
        mutations.append(lambda p: p['records'][0]['upload']['vertex'].__setitem__('offset', 1))
        mutations.append(lambda p: p['records'][1]['upload']['index'].__setitem__('bytes', 5638))
        mutations.append(lambda p: p['records'][1]['upload'].__setitem__('owner', '0020000000000004'))
        mutations.append(lambda p: p['records'][0]['upload'].__setitem__('mapped_pointer', '0020000000000005'))
        mutations.append(lambda p: p['geometry'].__setitem__('copy_ticks', True))
        mutations.append(lambda p: p['records'][0]['upload']['vertex'].__setitem__('offset', True))
        mutations.append(lambda p: p['records'][0]['upload'].__setitem__('arm_serial', '20000000000001'))
        for mutate in mutations:
            p = payload_packet(); mutate(p)
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                validate(p)
        validate(refusal_packet(), require_complete=True)
        p = refusal_packet(); p['records'][0]['upload']['owner'] = '0020000000000002'
        with self.assertRaisesRegex(ValueError, 'unexpected/missing refused'):
            validate(p)

    def test_corrupt_size_hash_symlink_and_out_of_range_index_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = self.write_bundle(root)
            sidecar = root / payload_packet()['geometry']['file']
            sidecar.write_bytes(geometry_bytes()[:-1])
            with self.assertRaisesRegex(ValueError, 'byte size'):
                load(path)
            sidecar.write_bytes(b'X' + geometry_bytes()[1:])
            with self.assertRaisesRegex(ValueError, 'SHA-256'):
                load(path)
            outside = root / 'outside.bin'; outside.write_bytes(geometry_bytes())
            sidecar.unlink(); sidecar.symlink_to(outside)
            with self.assertRaises(OSError):
                load(path)
            sidecar.unlink()
            data = bytearray(geometry_bytes())
            offset = GEOMETRY_RANGES[1]['index'][0]
            data[offset:offset + 2] = (GEOMETRY_RANGES[1]['vertices']).to_bytes(2, 'little')
            path = self.write_bundle(root, data=bytes(data))
            with self.assertRaisesRegex(ValueError, 'index outside'):
                load(path)

    def test_unavailable_geometry_cannot_reference_a_path_or_claim_success(self):
        for mutate in (
                lambda p: p['geometry'].__setitem__('file', '../payload.bin'),
                lambda p: p.__setitem__('payload_copy_valid', True),
                lambda p: p['records'][0]['upload'].__setitem__('invalidated_by', 'reset')):
            p = refusal_packet(); mutate(p)
            with self.subTest(mutate=mutate), self.assertRaises(ValueError):
                validate(p)

    def test_packet_invalidation_must_name_the_actual_terminal_status(self):
        incomplete = refusal_packet()
        incomplete['status'] = 'reset'
        with self.assertRaisesRegex(ValueError, 'terminal state did not invalidate every attachment'):
            validate(incomplete)
        p = refusal_packet()
        p['status'] = 'reset'
        for record in p['records']:
            record['upload'] = {
                'pair_status': 'copied', 'attachment_status': 'packet_invalid',
                'producer_payload_valid': False,
                'binding_revision_match_at_observation': False, 'invalidated_by': 'reset'}
        validate(p)
        p['records'][0]['upload']['invalidated_by'] = 'partial'
        with self.assertRaisesRegex(ValueError, 'attachment invalidation'):
            validate(p)
        p['records'][0]['upload']['invalidated_by'] = 'reset'
        p['status'] = 'complete'
        with self.assertRaisesRegex(ValueError, 'attachment invalidation'):
            validate(p)

    def test_schema2_state_filename_must_match_json_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = self.write_bundle(root)
            renamed = root / 'lattice-state-999-1-8-0.json'
            path.rename(renamed)
            with self.assertRaisesRegex(ValueError, 'basename/identity mismatch'):
                load(renamed)
            schema1 = state_packet()
            renamed.write_text(json.dumps(schema1))
            self.assertEqual(load(renamed), schema1)


if __name__ == '__main__':
    unittest.main()
