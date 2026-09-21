"""Snapshot selection, strict path authorization and failure behavior; no Wine."""
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from verification.analysis.test_lattice_payload_packet import payload_packet, geometry_bytes

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('snapshot_x3_run', ROOT / 'tools/analysis/snapshot_x3_run.py')
snapshot = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(snapshot)


def fnv(data):
    result = 14695981039346656037
    for byte in data:
        result = ((result ^ byte) * 1099511628211) & 0xffffffffffffffff
    return result


class SnapshotX3RunTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='x3-snapshot-host-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.capture = self.root / 'captures'; self.capture.mkdir()
        self.output = self.root / 'output'; self.output.mkdir()
        self.log = self.capture / 'session-20260913-120000-123.log'
        self.log.write_text('')  # Created before its frame readbacks.
        self.idle = patch.object(snapshot, 'game_running', return_value=[])
        self.idle.start(); self.addCleanup(self.idle.stop)

    def save(self, **kwargs):
        return snapshot.snapshot(capture_dir=self.capture, destination_root=self.output, **kwargs)

    def write_readback(self, name='hdr_1_2.rgba16f', data=b'abcd', **fields):
        (self.capture / name).write_bytes(data)
        row = dict(device='1', frame='2', file=name, result='00000000', bytes=str(len(data)))
        row.update(fields)
        return 'hdr_readback ' + ' '.join(f'{key}={value}' for key, value in row.items()) + '\n'

    def test_lattice_state_packet_copied_only_from_successful_bound_writer(self):
        name = 'lattice-state-123-1-2-0.json'
        payload = b'{"status":"unavailable"}\n'
        (self.capture / name).write_bytes(payload)
        row = f'lattice_state pid=123 device=1 frame=2 generation=0 file={name} bytes={len(payload)} file_ok=1 status=unavailable\n'
        self.log.write_text(row)
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (1, []))
        self.assertEqual((destination/name).read_bytes(), payload)
        for replacement in [row.replace('file_ok=1', 'file_ok=0'),
                            row.replace('device=1', 'device=9'),
                            row.replace(name, '../'+name),
                            row.replace(f'bytes={len(payload)}', 'bytes=1048577')]:
            wanted, issues = snapshot.references([replacement])
            self.assertEqual(wanted, {})
            self.assertTrue(issues)
        wanted, issues = snapshot.references([row, row.replace('file_ok=1', 'file_ok=0')])
        self.assertEqual(wanted, {})
        self.assertTrue(issues)

    def test_lattice_state_stale_or_size_mismatch_is_not_preserved(self):
        name = 'lattice-state-123-1-2-0.json'
        asset = self.capture/name
        asset.write_bytes(b'{}')
        self.log.write_text(f'lattice_state pid=123 device=1 frame=2 generation=0 file={name} bytes=3 file_ok=1\n')
        destination, count, issues = self.save(log=self.log)
        self.assertEqual(count, 0)
        self.assertTrue(issues)
        self.assertFalse((destination/name).exists())
        self.log.write_text(self.log.read_text().replace('bytes=3', 'bytes=2'))
        os.utime(asset, ns=(1, 1))
        destination, count, issues = self.save(log=self.log)
        self.assertEqual(count, 0)
        self.assertTrue(issues)
        self.assertFalse((destination/name).exists())

    def write_lattice_bundle(self):
        packet = payload_packet()
        state_name = 'lattice-state-123-1-8-0.json'
        payload_name = packet['geometry']['file']
        raw = json.dumps(packet).encode()
        (self.capture / state_name).write_bytes(raw)
        (self.capture / payload_name).write_bytes(geometry_bytes())
        row = (f'lattice_state pid=123 device=1 frame=8 generation=0 file={state_name} '
               f'bytes={len(raw)} file_ok=1 status=complete matches=1,1 schema=2 '
               f'payload_copy_valid=1 payload_file={payload_name} payload_bytes={len(geometry_bytes())} '
               f'payload_sha256={hashlib.sha256(geometry_bytes()).hexdigest()}\n')
        return packet, state_name, payload_name, row

    def test_schema2_bundle_is_hash_checked_cross_checked_and_collected(self):
        _, state_name, payload_name, row = self.write_lattice_bundle()
        self.log.write_text(row)
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (2, []))
        self.assertEqual((destination / payload_name).stat().st_size, 466224)
        self.assertTrue((destination / state_name).exists())

        # A valid binary under a log hash cannot survive JSON metadata that
        # names a different hash; the diagnostic JSON remains available.
        packet = payload_packet()
        packet['geometry']['sha256'] = '1' * 64
        raw = json.dumps(packet).encode()
        (self.capture / state_name).write_bytes(raw)
        self.log.write_text(row.replace(row.split('bytes=', 1)[1].split(' ', 1)[0], str(len(raw)), 1))
        destination, count, issues = self.save(log=self.log)
        self.assertEqual(count, 1)
        self.assertTrue(issues)
        self.assertTrue((destination / state_name).exists())
        self.assertFalse((destination / payload_name).exists())

    def test_schema2_json_only_refusal_is_preserved_without_payload(self):
        from verification.analysis.test_lattice_payload_packet import refusal_packet
        packet = refusal_packet()
        state_name = 'lattice-state-123-1-8-0.json'
        raw = json.dumps(packet).encode()
        (self.capture / state_name).write_bytes(raw)
        self.log.write_text(
            f'lattice_state pid=123 device=1 frame=8 generation=0 file={state_name} bytes={len(raw)} '
            'file_ok=1 status=complete matches=1,1 schema=2 payload_copy_valid=0 '
            'payload_file=- payload_bytes=0 payload_sha256=-\n')
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (1, []))
        self.assertTrue((destination / state_name).exists())
        self.assertFalse(any(path.name.startswith('lattice-geometry-') for path in destination.iterdir()))

    def test_malformed_nested_schema2_json_remains_but_binary_is_removed(self):
        for message, mutate in (
                ('record is not an object', lambda p: p['records'].__setitem__(0, None)),
                ('field is not an object', lambda p: p['records'][0]['fields'].__setitem__(0, None))):
            packet, state_name, payload_name, row = self.write_lattice_bundle()
            mutate(packet)
            raw = json.dumps(packet).encode()
            (self.capture / state_name).write_bytes(raw)
            old_size = row.split('bytes=', 1)[1].split(' ', 1)[0]
            self.log.write_text(row.replace(f'bytes={old_size}', f'bytes={len(raw)}', 1))
            destination, count, issues = self.save(log=self.log)
            with self.subTest(message=message):
                self.assertEqual(count, 1)
                self.assertTrue(any(message in issue for issue in issues))
                self.assertTrue((destination / state_name).exists())
                self.assertFalse((destination / payload_name).exists())

    def test_bounded_copy_rejects_growth_before_writing_extra_bytes(self):
        name = 'lattice-geometry-123-1-8-0.bin'
        admitted = b'abcd'
        (self.capture / name).write_bytes(admitted + b'x')
        source_fd = os.open(self.capture, os.O_RDONLY | os.O_DIRECTORY)
        target = self.root / 'bounded-target'; target.mkdir()
        target_fd = os.open(target, os.O_RDONLY | os.O_DIRECTORY)
        actual = (self.capture / name).stat()
        before = SimpleNamespace(**{key: getattr(actual, key) for key in
                                   ('st_mode', 'st_dev', 'st_ino', 'st_mtime_ns', 'st_ctime_ns')},
                                 st_size=len(admitted))
        try:
            with patch.object(snapshot.os, 'fstat', return_value=before):
                with self.assertRaisesRegex(ValueError, 'grew while copying'):
                    snapshot.copy_file(source_fd, target_fd, name, size=len(admitted),
                                       expected_sha256=hashlib.sha256(admitted).hexdigest())
        finally:
            os.close(target_fd); os.close(source_fd)
        self.assertFalse((target / name).exists())

    def test_schema2_identity_path_and_later_failure_revoke_payload(self):
        _, state_name, payload_name, row = self.write_lattice_bundle()
        bad_path = row.replace(f'payload_file={payload_name}', 'payload_file=../' + payload_name)
        wanted, issues = snapshot.references([bad_path])
        self.assertEqual(wanted, {})
        self.assertTrue(issues)
        bad_identity = row.replace('payload_file=lattice-geometry-123-',
                                   'payload_file=lattice-geometry-124-')
        wanted, issues = snapshot.references([bad_identity])
        self.assertEqual(wanted, {})
        self.assertTrue(issues)
        failed = row.replace('file_ok=1', 'file_ok=0').replace('payload_copy_valid=1', 'payload_copy_valid=0')
        wanted, issues = snapshot.references([row, failed])
        self.assertEqual(wanted, {})
        self.assertTrue(issues)
        # An unlogged same-shape binary is never discovered by directory scan.
        orphan = self.capture / 'lattice-geometry-999-1-8-0.bin'
        orphan.write_bytes(geometry_bytes())
        self.log.write_text(row)
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (2, []))
        self.assertFalse((destination / orphan.name).exists())

    def test_log_first_and_only_referenced_assets_saved_without_overwrite(self):
        record = self.write_readback()
        shader = b'original synthetic shader'; shader_id = f'{fnv(shader):016x}'
        (self.capture / f'ps_{shader_id}.bin').write_bytes(shader)
        (self.capture / 'mesh-adjacency-1.bin').write_bytes(b'mesh')
        self.log.write_text(record + f'shader kind=ps id={shader_id} bytes={len(shader)} dumped=1\n'
                            + 'mesh_adjacency_dump index=1 written=1 path=mesh-adjacency-1.bin\n'
                            + 'unrelated file=private.txt\n')
        (self.capture / 'private.txt').write_text('unrelated')
        calls = []
        original_copy = snapshot.copy_file
        def observed(*args, **kwargs):
            calls.append(args[2])
            return original_copy(*args, **kwargs)
        with patch.object(snapshot, 'copy_file', side_effect=observed):
            destination, count, issues = self.save(log=self.log)
        self.assertEqual(calls[0], self.log.name)
        self.assertEqual((count, issues), (3, []))
        self.assertEqual((destination / self.log.name).read_bytes(), self.log.read_bytes())
        self.assertFalse((destination / 'private.txt').exists())
        self.assertEqual(len(list(destination.iterdir())), 4)  # No extra manifest.
        second, _, _ = self.save(log=self.log)
        self.assertEqual((destination.name, second.name), ('x3-bottleX3-run1', 'x3-bottleX3-run2'))
        self.assertTrue((self.capture / 'hdr_1_2.rgba16f').exists())

    def test_launcher_stderr_is_preserved_and_counted(self):
        launcher = self.capture / snapshot.LAUNCHER_STDERR
        launcher.write_text('[2026-09-16T10:00:00.000Z] GStreamer-CRITICAL\n')
        touched = snapshot.created_ns(self.log.stat()) + 1
        os.utime(launcher, ns=(touched, touched))
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (1, []))
        self.assertEqual((destination / snapshot.LAUNCHER_STDERR).read_text(),
                         '[2026-09-16T10:00:00.000Z] GStreamer-CRITICAL\n')
        # Absent (a session not started through tools/manage.py launch) is silent.
        launcher.unlink()
        self.assertEqual(self.save(log=self.log)[1:], (0, []))

    def test_launcher_stderr_of_a_later_launch_is_refused_with_an_explicit_log(self):
        launcher = self.capture / snapshot.LAUNCHER_STDERR
        launcher.write_text('later launch\n')
        # created_ns sees stat results only, so the two files are told apart by size.
        with patch.object(snapshot, 'created_ns',
                          side_effect=lambda info: 20 if info.st_size == launcher.stat().st_size else 10):
            destination, count, issues = self.save(log=self.log)
        self.assertEqual(count, 0)
        self.assertEqual(len(issues), 1)
        self.assertIn('belongs to a later launch', issues[0])
        self.assertFalse((destination / snapshot.LAUNCHER_STDERR).exists())

    def test_launcher_stderr_untouched_after_the_log_is_preserved_with_a_note(self):
        launcher = self.capture / snapshot.LAUNCHER_STDERR
        launcher.write_text('[2026-09-16T10:00:00.000Z] launcher_tee pid=42\n')
        older = self.log.stat().st_mtime_ns - 1_000_000
        os.utime(launcher, ns=(older, older))
        destination, count, issues = self.save(log=self.log)
        self.assertEqual(count, 1)
        self.assertTrue((destination / snapshot.LAUNCHER_STDERR).exists())
        self.assertEqual(len(issues), 1)
        self.assertIn('not written after the session log was created', issues[0])

    def test_launcher_stderr_from_an_earlier_launch_is_reported_not_copied(self):
        stale = self.capture / snapshot.LAUNCHER_STDERR
        stale.write_text('old\n')
        boundary = max(snapshot.created_ns(stale.stat()), stale.stat().st_mtime_ns) + 1
        with patch.object(snapshot, 'created_ns', side_effect=lambda info: info.st_mtime_ns):
            os.utime(self.log, ns=(boundary + 1, boundary + 1))
            destination, count, issues = self.save(since_ns=boundary)
        self.assertEqual(count, 0)
        self.assertEqual(len(issues), 1)
        self.assertIn(snapshot.LAUNCHER_STDERR, issues[0])
        self.assertFalse((destination / snapshot.LAUNCHER_STDERR).exists())

    def test_since_selects_newest_and_no_new_log_is_noop(self):
        boundary = snapshot.created_ns(self.log.stat()) + 1
        with patch.object(snapshot, 'created_ns', side_effect=lambda info: info.st_mtime_ns):
            os.utime(self.log, ns=(boundary-1, boundary-1))
            self.assertIsNone(self.save(since_ns=boundary)[0])
            newer = self.capture / 'session-20260913-130000-124.log'; newer.write_text('newer\n')
            os.utime(newer, ns=(boundary+2, boundary+2))
            newest = self.capture / 'session-20260913-140000-125.log'; newest.write_text('newest\n')
            os.utime(newest, ns=(boundary+3, boundary+3))
            destination, _, _ = self.save(since_ns=boundary)
        self.assertTrue((destination / newest.name).exists())
        self.assertFalse((destination / newer.name).exists())
        with self.assertRaises(ValueError): self.save(since_ns=0)

    def test_birthtime_prevents_touched_old_session_selection(self):
        info = SimpleNamespace(st_birthtime_ns=10, st_mtime_ns=30, st_mode=0o100600)
        with patch.object(snapshot.os, 'listdir', return_value=[self.log.name]), patch.object(snapshot.os, 'stat', return_value=info):
            self.assertIsNone(snapshot.select_log(0, 20))
        self.assertEqual(snapshot.created_ns(SimpleNamespace(st_ctime_ns=25)), 25)

    def test_selected_log_replaced_with_old_log_refuses_before_parsing(self):
        boundary = self.log.stat().st_mtime_ns + 1
        with patch.object(snapshot, 'select_log', return_value=self.log.name), patch.object(snapshot, 'references') as parse:
            with self.assertRaisesRegex(ValueError, 'predates this launch'):
                self.save(since_ns=boundary)
        parse.assert_not_called()
        self.assertEqual(list((self.output / 'x3-bottleX3-run1').iterdir()), [])

    def test_actual_writer_record_shapes_cover_all_readback_formats(self):
        # Full emitted field order/format from readback_surface; synthetic bytes.
        rows, expected = [], 0
        for tag, (prefix, extensions) in snapshot.READBACKS.items():
            for index, extension in enumerate(extensions):
                name = f'{prefix}_1_{2 + index}.{extension}'
                (self.capture / name).write_bytes(b'abcd')
                rows.append(f'{tag} device=1 frame={2 + index} file={name} width=1 height=1 '
                            f'format={extension}_row_major result=00000000 bytes=4\n')
                expected += 1
        self.log.write_text(''.join(rows))
        _, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (expected, []))
        self.assertEqual(expected, 9)  # Seven writers; the depth tag has three formats (r32f, rg32f, rgba32f).

    def test_sun_lane_depth_and_shadow_map_records_are_preserved(self):
        # Sun lane active: RT2 is G32R32F (.r depth, .g share) and the replayed
        # shadow map is dumped as R32F; both are the real logged lines.
        (self.capture / 'depth_1_8979.rg32f').write_bytes(b'ab' * 6)
        (self.capture / 'shadow_map_1_8979.r32f').write_bytes(b'cd' * 4)
        self.log.write_text(
            'motion_output_depth_readback device=1 frame=8979 file=depth_1_8979.rg32f '
            'width=1280 height=768 format=rg32f_row_major result=00000000 bytes=12\n'
            'shadow_replay_map_readback device=1 frame=8979 file=shadow_map_1_8979.r32f '
            'width=1024 height=1024 format=r32f_row_major result=00000000 bytes=8\n')
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (2, []))
        self.assertEqual((destination / 'depth_1_8979.rg32f').read_bytes(), b'ab' * 6)
        self.assertEqual((destination / 'shadow_map_1_8979.r32f').read_bytes(), b'cd' * 4)

    def test_per_cascade_shadow_map_names_are_preserved_and_bad_ones_refused(self):
        # Real cascade dumps glue the cascade index to the prefix; the
        # single-map name stays valid and nothing else does.
        rows = []
        for cascade in range(snapshot.CASCADES):
            name = f'shadow_map{cascade}_1_11954.r32f'
            (self.capture / name).write_bytes(b'ef' * 4)
            rows.append(f'shadow_replay_map_readback device=1 frame=11954 file={name} '
                        f'width=1024 height=1024 format=r32f_row_major result=00000000 bytes=8\n')
        (self.capture / 'shadow_map_1_11954.r32f').write_bytes(b'ef' * 4)
        rows.append('shadow_replay_map_readback device=1 frame=11954 file=shadow_map_1_11954.r32f '
                    'width=1024 height=1024 format=r32f_row_major result=00000000 bytes=8\n')
        self.log.write_text(''.join(rows))
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (snapshot.CASCADES + 1, []))
        self.assertEqual((destination / 'shadow_map0_1_11954.r32f').read_bytes(), b'ef' * 4)
        for name in ('shadow_map8_1_2.r32f', 'shadow_map00_1_2.r32f', 'shadow_map0_1_2.r32f/../x',
                     '../shadow_map0_1_2.r32f', 'shadow_map0/../1_2.r32f', 'depth0_1_2.r32f'):
            with self.subTest(name=name):
                tag = 'motion_output_depth_readback' if name.startswith('depth') else 'shadow_replay_map_readback'
                self.log.write_text(f'{tag} device=1 frame=2 file={name} result=00000000 bytes=4\n')
                destination, count, issues = self.save(log=self.log)
                self.assertEqual(count, 0); self.assertTrue(issues)
                self.assertEqual([path.name for path in destination.iterdir()], [self.log.name])

    def test_readback_basename_longer_than_the_bound_is_refused(self):
        name = 'shadow_map0_1_' + '1' * snapshot.MAX_BASENAME + '.r32f'
        self.log.write_text('shadow_replay_map_readback device=1 frame=' + '1' * snapshot.MAX_BASENAME
                            + f' file={name} result=00000000 bytes=4\n')
        _, count, issues = self.save(log=self.log)
        self.assertEqual(count, 0)
        self.assertTrue(any('unsafe or unexpected readback basename' in issue for issue in issues))

    def test_wide_depth_dump_is_copied(self):
        (self.capture / 'depth_1_8979.rgba32f').write_bytes(b'abcd' * 8)
        self.log.write_text('motion_output_depth_readback device=1 frame=8979 file=depth_1_8979.rgba32f '
                            'width=1280 height=768 format=rgba32f_row_major result=00000000 bytes=32\n')
        destination, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (1, []))
        self.assertEqual((destination / 'depth_1_8979.rgba32f').read_bytes(), b'abcd' * 8)

    def test_extension_outside_the_tag_allowance_is_refused(self):
        for tag, name in (('motion_output_depth_readback', 'depth_1_2.rgb32f'),
                          ('shadow_replay_map_readback', 'shadow_map_1_2.rg32f'),
                          ('hdr_readback', 'hdr_1_2.rg32f'),
                          ('motion_output_depth_readback', 'depth_1_2.r32f.rg32f'),
                          ('shadow_replay_map_readback', 'shadow_map_1_3.r32f')):
            with self.subTest(name=name):
                (self.capture / name).write_bytes(b'abcd')
                self.log.write_text(f'{tag} device=1 frame=2 file={name} result=00000000 bytes=4\n')
                destination, count, issues = self.save(log=self.log)
                self.assertEqual(count, 0); self.assertTrue(issues)
                self.assertEqual([path.name for path in destination.iterdir()], [self.log.name])

    def test_running_game_or_unknown_inventory_refuses_before_creation(self):
        for state in (['123 X3AP.exe'], RuntimeError('process inventory unavailable')):
            with self.subTest(state=state), patch.object(snapshot, 'game_running', side_effect=state if isinstance(state, Exception) else None,
                                                         return_value=state if not isinstance(state, Exception) else None):
                with self.assertRaises(RuntimeError): self.save(log=self.log)
        self.assertEqual(list(self.output.iterdir()), [])

    def test_traversal_absolute_backslash_and_wrong_writer_names_never_copied(self):
        for name in ('../private.txt', '/private.txt', '..\\private.txt', 'private.txt', 'hdr_99_2.rgba16f'):
            with self.subTest(name=name):
                self.log.write_text('hdr_readback device=1 frame=2 file=' + name + ' result=00000000 bytes=4\n')
                destination, count, issues = self.save(log=self.log)
                self.assertEqual(count, 0); self.assertTrue(issues)
                self.assertEqual([p.name for p in destination.iterdir()], [self.log.name])

    def test_symlink_readback_log_and_capture_directory_refused(self):
        outside = self.root / 'outside'; outside.write_bytes(b'abcd')
        (self.capture / 'hdr_1_2.rgba16f').symlink_to(outside)
        self.log.write_text('hdr_readback device=1 frame=2 file=hdr_1_2.rgba16f result=00000000 bytes=4\n')
        _, count, issues = self.save(log=self.log)
        self.assertEqual(count, 0); self.assertTrue(issues)
        linked = self.capture / 'session-20260913-140000-456.log'; linked.symlink_to(self.log)
        with self.assertRaises(OSError): self.save(log=linked)
        linked_dir = self.root / 'linked'; linked_dir.symlink_to(self.capture, target_is_directory=True)
        with self.assertRaises(OSError): snapshot.snapshot(capture_dir=linked_dir, since_ns=1, destination_root=self.output)

    def test_missing_failed_stale_and_size_mismatched_readbacks_are_reported(self):
        for failure in ('missing', 'failed', 'stale', 'overwritten', 'size'):
            with self.subTest(failure=failure):
                row = self.write_readback()
                self.log.write_text(row)
                source = self.capture / 'hdr_1_2.rgba16f'
                if failure == 'missing': source.unlink()
                elif failure == 'failed': self.log.write_text(row.replace('result=00000000', 'result=80004005'))
                elif failure == 'stale': os.utime(source, ns=(1, 1))
                elif failure == 'overwritten': os.utime(source, ns=(self.log.stat().st_mtime_ns+1000000,)*2)
                else: self.log.write_text(row.replace('bytes=4', 'bytes=100'))
                destination, count, issues = self.save(log=self.log)
                self.assertEqual(count, 0); self.assertTrue(issues)
                self.assertTrue((destination / self.log.name).exists())
                self.assertFalse((destination / source.name).exists())

    def test_shader_wrong_identity_and_oversize_are_not_preserved(self):
        shader = 'ps_0000000000000000.bin'; (self.capture / shader).write_bytes(b'bad')
        self.log.write_text('shader kind=ps id=0000000000000000 bytes=3 dumped=1\n')
        destination, count, issues = self.save(log=self.log)
        self.assertEqual(count, 0); self.assertTrue(issues); self.assertFalse((destination / shader).exists())
        self.log.write_text('shader kind=ps id=0000000000000000 bytes=5000000 dumped=1\n')
        _, count, issues = self.save(log=self.log)
        self.assertEqual(count, 0); self.assertTrue(issues)

    def test_copy_failure_keeps_originals_and_existing_snapshot(self):
        self.log.write_text(self.write_readback())
        first = self.output / 'x3-bottleX3-run1'; first.mkdir(); (first / 'keep').write_text('keep')
        original = snapshot.copy_file
        def fail(source, target, name, **kwargs):
            if name == 'hdr_1_2.rgba16f': raise OSError('disk full')
            return original(source, target, name, **kwargs)
        with patch.object(snapshot, 'copy_file', side_effect=fail):
            destination, count, issues = self.save(log=self.log)
        self.assertEqual(destination.name, 'x3-bottleX3-run2')
        self.assertEqual((count, len(issues)), (0, 1)); self.assertTrue((first / 'keep').exists())
        self.assertEqual((self.capture / 'hdr_1_2.rgba16f').read_bytes(), b'abcd')

    def test_numbering_skips_holes_and_retries_exclusive_creation_race(self):
        for number in (5, 17):
            (self.output / f'x3-bottleX3-run{number}').mkdir()
        original_mkdir = Path.mkdir
        competing = self.output / 'x3-bottleX3-run18'
        def race(path, *args, **kwargs):
            if path == competing and not path.exists():
                original_mkdir(path)
                (path / 'keep').write_text('concurrent snapshot')
            return original_mkdir(path, *args, **kwargs)
        with patch.object(Path, 'mkdir', new=race):
            destination, _, _ = self.save(log=self.log)
        self.assertEqual(destination.name, 'x3-bottleX3-run19')
        self.assertEqual((competing / 'keep').read_text(), 'concurrent snapshot')
        self.assertFalse((self.output / 'x3-bottleX3-run1').exists())

    def test_launcher_preserves_arguments_and_game_exit_even_if_snapshot_fails(self):
        executable = self.root / 'python3'
        executable.write_text('#!/bin/sh\nprintf "<%s>\\n" "$PWD" "$@" >> "$X3_TEST_CALLS"\ncase "$1" in\n-c) echo 123;;\nverification/probe/wine_lock.py) exit "$X3_TEST_GAME_STATUS";;\ntools/analysis/snapshot_x3_run.py) exit 2;;\n*) exit 99;;\nesac\n')
        executable.chmod(0o700)
        calls = self.root / 'calls.txt'
        for status in (0, 17):
            calls.write_text('')
            completed = subprocess.run([str(ROOT / 'x3run'), '--direct', '--camera', 'chase', 'argument with spaces'],
                                       cwd=self.root,
                                       env=dict(os.environ, PATH=str(self.root) + os.pathsep + os.environ['PATH'],
                                                X3_TEST_GAME_STATUS=str(status), X3_TEST_CALLS=str(calls)), capture_output=True, text=True)
            self.assertEqual(completed.returncode, status, completed.stderr)
            invoked = calls.read_text()
            self.assertEqual(invoked.count(f'<{ROOT}>\n'), 3)
            self.assertIn('<verification/probe/wine_lock.py>\n<--holder>\n<user-game>\n<python3>\n<tools/manage.py>\n<launch>\n<--bottle>\n<X3>\n<--direct>\n<--camera>\n<chase>\n<argument with spaces>\n', invoked)
            self.assertIn('<tools/analysis/snapshot_x3_run.py>\n<--since-ns>\n<123>\n', invoked)


if __name__ == '__main__':
    unittest.main()
