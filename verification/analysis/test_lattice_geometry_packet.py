#!/usr/bin/env python3
"""Host production packet/writer fixture validated by the actual schema-2 reader."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from lattice_state_packet import load, GEOMETRY_BYTES, GEOMETRY_RANGES, PAIR_STATUSES
from verification.analysis.test_lattice_state_capture import packet as state_packet

ROOT = Path(__file__).resolve().parents[2]


def authored_bytes():
    result = bytearray(GEOMETRY_BYTES)
    for slot, ranges in enumerate(GEOMETRY_RANGES):
        offset, size = ranges['vertex']
        result[offset:offset+size] = bytes((i+17*slot) % 251 for i in range(size))
        offset, size = ranges['index']
        for i in range(0, size, 2):
            result[offset+i:offset+i+2] = ((i//2) % ranges['vertices']).to_bytes(2, 'little')
    return bytes(result)


class GeometryWriterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.exe = cls.root/'packet-fixture'
        subprocess.run([shutil.which('clang++') or 'c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT/'verification/probe/lattice_geometry_packet_host.cpp'), '-o', str(cls.exe)],
                       check=True, capture_output=True, text=True)
        cls.payload = authored_bytes()
        cls.digest = hashlib.sha256(cls.payload).hexdigest()
        for slot, record in enumerate(state_packet()['records']):
            (cls.root/f'fields-{slot}.json').write_text(json.dumps(record['fields']))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_case(self, mode):
        directory = self.root/mode
        command = [str(self.exe), mode, str(directory), str(self.root/'fields-0.json'),
                   str(self.root/'fields-1.json'), self.digest]
        run = subprocess.run(command, check=True, capture_output=True, text=True)
        report = json.loads(run.stdout)
        self.assertEqual(report['failures'], 0)
        path = directory/'lattice-state-123-1-8-0.json'
        parsed = load(path) if report['file_ok'] else None
        return directory, report, parsed

    def test_exact_positive_reversed_slots_and_large_ids(self):
        directory, report, parsed = self.run_case('okay')
        self.assertTrue(report['payload_valid'])
        self.assertEqual(load(directory/'lattice-state-123-1-8-0.json', True, True), parsed)
        self.assertEqual((directory/parsed['geometry']['file']).read_bytes(), self.payload)
        self.assertEqual(parsed['geometry']['bytes'], 466224)
        self.assertEqual(parsed['geometry']['sha256'], self.digest)
        self.assertEqual(parsed['geometry']['copy_ticks'], 16)
        self.assertGreater(int(parsed['records'][0]['upload']['owner'], 16), 2**53)

    def test_every_pair_refusal_erases_successful_sibling(self):
        for reason in sorted(PAIR_STATUSES-{'copied', 'not_attempted'}):
            with self.subTest(reason=reason):
                directory, report, parsed = self.run_case('refuse_'+reason)
                self.assertFalse(report['payload_valid'])
                self.assertEqual(report['hashes'], 0)
                self.assertEqual(parsed['status'], 'complete')
                self.assertEqual(parsed['records'][1]['upload']['pair_status'], reason)
                self.assertEqual(parsed['records'][1]['upload']['attachment_status'], 'copy_refused')
                self.assertEqual(parsed['records'][0]['upload']['attachment_status'], 'sibling_refused')
                self.assertFalse(list(directory.glob('*.bin')))

    def test_terminal_invalidation_cannot_be_revived(self):
        for reason in ['reset','ambiguous','partial','unavailable','capacity','submission_failed','no_match','reentrant_partial','suppressed']:
            with self.subTest(reason=reason):
                directory, report, parsed = self.run_case(reason)
                self.assertFalse(report['payload_valid'])
                self.assertEqual(report['hashes'], 0)
                for record in parsed['records']:
                    self.assertEqual(record['upload']['attachment_status'], 'packet_invalid')
                    self.assertEqual(record['upload']['invalidated_by'], parsed['status'])
                    self.assertNotIn('vertex', record['upload'])
                self.assertFalse(list(directory.glob('*.bin')))

    def test_allocation_and_cross_pair_identity_refusal(self):
        for reason in ['allocation_failure','arm_mismatch','owner_mismatch','generation_mismatch',
                       'same_invocation','allocation_collision','malformed_bytes']:
            with self.subTest(reason=reason):
                directory, report, parsed = self.run_case(reason)
                self.assertFalse(report['payload_valid'])
                self.assertEqual(report['hashes'], 0)
                self.assertTrue(all(not r['upload']['producer_payload_valid'] for r in parsed['records']))
                self.assertFalse(list(directory.glob('*.bin')))
        parsed = load(self.root/'allocation_failure/lattice-state-123-1-8-0.json')
        self.assertEqual([r['upload']['attachment_status'] for r in parsed['records']], ['allocation_failure']*2)

    def test_payload_stage_failures_publish_refusal_only(self):
        for reason in ['hash','payload_open','payload_write','payload_close','payload_rename']:
            with self.subTest(reason=reason):
                directory, report, parsed = self.run_case(reason)
                self.assertTrue(report['file_ok'])
                self.assertFalse(report['payload_valid'])
                self.assertIsNone(parsed['geometry']['file'])
                self.assertTrue(all(r['upload']['attachment_status']=='export_failed' for r in parsed['records']))
                self.assertFalse(list(directory.glob('*.bin')))
                self.assertFalse(list(directory.glob('*.tmp')))

    def test_json_stage_failures_remove_only_owned_orphan(self):
        for reason in ['json_open','json_write','json_close','json_rename']:
            with self.subTest(reason=reason):
                directory, report, parsed = self.run_case(reason)
                self.assertFalse(report['file_ok'])
                self.assertFalse(report['payload_valid'])
                self.assertIsNone(parsed)
                self.assertEqual(list(directory.iterdir()), [])

    def test_collisions_preserve_existing_files(self):
        for kind in ['payload','json']:
            for location in ['temp','final']:
                reason = f'collision_{kind}_{location}'
                with self.subTest(reason=reason):
                    directory, report, parsed = self.run_case(reason)
                    name = ('lattice-geometry-123-1-8-0.bin' if kind=='payload' else 'lattice-state-123-1-8-0.json')
                    path = directory/(name+('.tmp' if location=='temp' else ''))
                    self.assertEqual(path.read_bytes(), b'pre-existing')
                    self.assertFalse(report['payload_valid'])
                    if kind=='payload':
                        self.assertTrue(report['file_ok'])
                        self.assertIsNone(parsed['geometry']['file'])
                    else:
                        self.assertFalse(report['file_ok'])
                        self.assertFalse((directory/'lattice-geometry-123-1-8-0.bin').exists())


if __name__ == '__main__':
    unittest.main()
