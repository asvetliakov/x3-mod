"""Original miniature archive fixtures; no copied game data."""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
from index_shaders import index
from sweep_shaders import extract, family_inventory, gpu_words, local_directory, sha


def tokens(*words):
    return struct.pack(f'<{len(words)}I', *words)


def archive(root, name, path, effect):
    cat = root / name
    cat.parent.mkdir(parents=True, exist_ok=True)
    text = f'{cat.with_suffix(".dat").name}\n{path} {len(effect)}\n'.encode()
    cat.write_bytes(bytes(byte ^ ((0xdb + i) & 255) for i, byte in enumerate(text)))
    cat.with_suffix('.dat').write_bytes(bytes(byte ^ 0x33 for byte in effect))


class ShaderSweepTests(unittest.TestCase):
    def test_comment_insensitive_code_group_is_separate_from_full_fingerprint(self):
        body = tokens(0x02000001, 0x800f0000, 0x90e40000, 0xffff)
        a = tokens(0xffff0300, 0x0001fffe, 0x12345678) + body
        b = tokens(0xffff0300, 0x0002fffe, 0xffff, 0xffff0300) + body
        self.assertNotEqual(sha(a), sha(b))
        self.assertEqual(gpu_words(a), gpu_words(b))

    def test_extended_sm2_and_sm1_phase_framing(self):
        for version in (0xffff0201, 0xfffe0201, 0xffff0104):
            tail = tokens(0xfffd) if version == 0xffff0104 else b''
            code = tokens(version, 0x0001fffe, 0xffff) + tail + tokens(0xffff)
            self.assertEqual(gpu_words(code), tokens(version) + tail + tokens(0xffff))
        with self.assertRaises(ValueError):
            gpu_words(tokens(0xffff0300, 0xffff, 0))

    def test_all_aliases_preserved_and_raw_program_deduplicated(self):
        with tempfile.TemporaryDirectory() as folder:
            temp = Path(folder)
            game = temp / 'game'
            shader = tokens(0xffff0300, 0x02000001, 0x800f0000, 0x90e40000, 0xffff)
            effect = tokens(0xfeff0901) + shader + shader
            archive(game, '01.cat', 'shader/3_0/original.fb', effect)
            archive(game, 'addon/01.cat', 'shader/2_a/alias.fb', effect)
            index_path = temp / 'index.json'
            index_path.write_text(json.dumps(index(game)))
            manifest_path = temp / 'manifest.json'
            with contextlib.redirect_stdout(io.StringIO()):
                extract(game, index_path, temp / 'raw', manifest_path)
            result = json.loads(manifest_path.read_text())
            self.assertEqual(len(result['programs']), 1)
            self.assertEqual(len(result['effects']), 2)
            self.assertEqual(len(list((temp / 'raw').glob('*.bin'))), 1)
            self.assertEqual([sum(e['program_occurrences'].values()) for e in result['effects']], [2, 2])
            stale = json.loads(index_path.read_text())
            stale['effects'].pop()
            index_path.write_text(json.dumps(stale))
            with self.assertRaisesRegex(ValueError, 'cover every'):
                extract(game, index_path, temp / 'raw', manifest_path)

    def test_raw_assets_cannot_be_written_under_repository_or_game(self):
        with tempfile.TemporaryDirectory() as folder:
            game = Path(folder) / 'game'
            with self.assertRaises(ValueError):
                local_directory(game / 'out', game)
            with self.assertRaises(ValueError):
                local_directory(Path(__file__).resolve().parent / 'raw-assets')

    def test_family_grouping_preserves_damage_and_toggle_alias_distinctions(self):
        effects = [{'path': path, 'effect_sha256': str(i), 'program_occurrences': {'vs_one': 2}}
                   for i, path in enumerate(('shader/3_0/xt_standard_lighting2s.fb',
                       'shader/2_a/hueshift_off/xt_standard_lighting_damage.fb',
                       'shader/1_1/xt_standard_lighting_damage2s.fb'))]
        programs = [{'id': 'vs_one', 'stage': 'vs', 'model': '1_1', 'runtime_captured': True}]
        result = family_inventory(effects, programs)
        self.assertEqual((result['family_count'], result['basename_count']), (2, 3))
        damage = result['families'][1]
        self.assertEqual(damage['family'], 'xt_standard_lighting_damage')
        self.assertEqual(damage['toggle_directories'], ['(base)', 'hueshift_off'])
        self.assertEqual(damage['stage_counts'], {'vs': 1})
        self.assertEqual(damage['effect_entries'], 2)
        self.assertEqual(damage['occurrences'], 4)


if __name__ == '__main__':
    unittest.main()
