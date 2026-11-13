"""Every shader program object MotionOutput can hold is counted by device_references() and released by
release_resources() (review B1, 2026-11-13: the muzzle effects pair's two programs were missing from the count, so
capture.cpp's final-Release test, after == held + 1, was off by two while they existed).

The list is not kept by hand: it is every IDirect3DVertexShader9* / IDirect3DPixelShader9* data member declared in
src/proxy/motion_output.h with the member suffix `_` (scalars and arrays). The shadow's non-owning bindings (struct
Shadow's `vs`, `ps`, ... without the suffix) and the per-shader entry variants (counted through vertex_ / pixel_) are
not members of this form. A new program member that the count or the release misses fails here.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/proxy/motion_output.h'
SOURCE = ROOT / 'src/proxy/motion_output.cpp'
MEMBER = re.compile(r'^\s+IDirect3D(?:Vertex|Pixel)Shader9\*\s+(\w+_)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*|\{\})?;', re.M)


def body(text, signature):
    """The brace-balanced body of the function whose definition starts with `signature`."""
    start = text.index(signature)
    i = text.index('{', start)
    depth = 0
    for j in range(i, len(text)):
        depth += {'{': 1, '}': -1}.get(text[j], 0)
        if depth == 0:
            return text[i:j + 1]
    raise AssertionError(f'unbalanced body: {signature}')


class ProgramReferences(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.members = MEMBER.findall(HEADER.read_text())
        source = SOURCE.read_text()
        cls.count = body(source, 'unsigned MotionOutput::device_references() const noexcept')
        cls.release = body(source, 'void MotionOutput::release_resources() noexcept')

    def test_the_member_list_is_found(self):
        # the members this test exists for, so a changed declaration style cannot empty the list unnoticed
        for name in ('muzzle_reshade_ps_', 'muzzle_effects_vs_', 'muzzle_effects_ps_', 'hull_reshade_ps_',
                     'bullet_reshade_ps_', 'quad_vs_', 'sentinel_ps_'):
            self.assertIn(name, self.members)
        self.assertEqual(len(self.members), len(set(self.members)))

    def test_every_program_member_is_counted(self):
        missing = [m for m in self.members if not re.search(r'\b' + m + r'\b', self.count)]
        self.assertEqual(missing, [], 'device_references() does not count these program members')

    def test_every_program_member_is_released(self):
        missing = [m for m in self.members if not re.search(r'\b' + m + r'\b', self.release)]
        self.assertEqual(missing, [], 'release_resources() does not release these program members')


if __name__ == '__main__':
    unittest.main()
