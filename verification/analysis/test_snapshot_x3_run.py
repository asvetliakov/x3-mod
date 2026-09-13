"""Snapshot selection, strict path authorization and failure behavior; no Wine."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

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
        rows = []
        for tag, (prefix, extension) in snapshot.READBACKS.items():
            name = f'{prefix}_1_2.{extension}'
            (self.capture / name).write_bytes(b'abcd')
            rows.append(f'{tag} device=1 frame=2 file={name} width=1 height=1 '
                        f'format={extension}_row_major result=00000000 bytes=4\n')
        self.log.write_text(''.join(rows))
        _, count, issues = self.save(log=self.log)
        self.assertEqual((count, issues), (6, []))

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
