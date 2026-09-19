"""The depth-only prepass table (src/renderer/depth_prepass_profiles.h) and its route wiring.

Static host checks, no game bytes: every row is a z_only alias of
docs/reverse-engineering/shader-fingerprints.md with the rigid position table's
identity (length, vs_1_1, clip rows in c0-3, row-dot position path), its clip
rows sit in a window the route already shadows (a motion-profile row uses the
same register), the route jitters a prepass program exactly where it jitters a
pair row's VS, and the per-frame summary carries unjittered_depth_writers.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/renderer/depth_prepass_profiles.h'
RIGID = ROOT / 'src/renderer/rigid_position_profiles_inc.h'
MOTION = ROOT / 'src/renderer/motion_output_profiles_inc.h'
FINGERPRINTS = ROOT / 'docs/reverse-engineering/shader-fingerprints.md'
PROXY = ROOT / 'src/proxy/motion_output.cpp'
ROW = re.compile(r'\{0x([0-9a-f]{16})ull, (\d+), 0x([0-9a-f]{8})u, (\d+)\}')


def rows():
    return [(h, int(n), v, int(r)) for h, n, v, r in ROW.findall(HEADER.read_text())]


class DepthPrepassProfileTests(unittest.TestCase):
    def test_rows_are_the_z_only_aliases(self):
        table = rows()
        doc = {h: int(size) for size, h in re.findall(r'\| z_only \| VS 1\.1 \| (\d+) \| `([0-9a-f]{16})` \|', FINGERPRINTS.read_text())}
        self.assertEqual(len(doc), 4, 'shader-fingerprints.md names four z_only aliases (z_only.fb and z_only_0000/0001.fb)')
        self.assertEqual({h: n * 4 for h, n, _, _ in table}, doc)
        self.assertTrue(all(v == 'fffe0101' and r == 0 for _, _, v, r in table), table)

    def test_rows_agree_with_the_rigid_position_table(self):
        rigid = RIGID.read_text()
        for h, n, _, r in rows():
            with self.subTest(hash=h):
                line = [l for l in rigid.splitlines() if l.startswith('{0x%sull' % h)]
                self.assertEqual(len(line), 1)
                self.assertIn('{0x%sull, %d, %d, true, 0xfffe0101u, PositionWriteOrder::XYZW, HomogeneousConstructor::MadXYZIdentityWFromX}' % (h, n, r), line[0])

    def test_clip_rows_sit_in_a_shadowed_window(self):
        motion_registers = {int(m) for m in re.findall(r'MotionOutputClass::\w+, (\d+), \d+, \{', MOTION.read_text())}
        for h, _, _, r in rows():
            with self.subTest(hash=h):
                self.assertIn(r, motion_registers)
        proxy = PROXY.read_text()
        self.assertIn('for (const auto& row : renderer::depth_prepass_profiles) add_matrix_window(windows, row.matrix_register);', proxy)
        self.assertIn('static_assert(prepass_rows_match_shadow()', proxy)

    def test_route_jitters_prepass_programs_and_counts_unjittered_depth_writers(self):
        proxy = PROXY.read_text()
        self.assertIn('if (jitter_active_ && (shadow_.vs_row || shadow_.vs_prepass)) apply_jitter(route);', proxy)
        self.assertIn('++counters_.unjittered_depth_writers;', proxy)
        self.assertIn('shadow_.vs_row ? shadow_.vs_row->matrix_register : shadow_.vs_prepass->matrix_register', proxy)
        # Registration: exclusive with a pair row; the version token is part of the identity.
        self.assertIn('if (!entry.row) entry.prepass = renderer::depth_prepass_vertex_row(hash, bytes / 4, code[0]);', proxy)
        # Gate 3 still needs a pair row: a prepass program never routes.
        self.assertIn('shadow_.vs_variant && shadow_.ps_variant && shadow_.vs_row', proxy)
        summary = [l for l in proxy.splitlines() if l.strip().startswith('log("motion_output_frame ')][0]
        self.assertIn(' jittered=%lu unjittered_depth_writers=%lu cut=%u', summary)


if __name__ == '__main__':
    unittest.main()
