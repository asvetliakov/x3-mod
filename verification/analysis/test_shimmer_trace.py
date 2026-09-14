"""Host checks of the distant-shimmer trace (parser and Asteroid pair table).

Synthetic shimmer_frame/shimmer_draw lines in the exact format
src/proxy/motion_output.cpp writes: field values, the per-frame truncation
count, the scaled projection terms, and the per-frame LOD change / vanished
identity a zoomed asteroid frame must expose
(docs/architecture/linear-distance-fade-region.md, "Shimmer trace (diagnostic)").
Plus the table invariant the trace classification rests on: exactly the six
Asteroid pairs of src/renderer/linear_material.cpp are Asteroid-class, and the
fade admission set is those six plus the station BUMPMAP pair
(driver verification/probe/asteroid_pair_table.cpp). No device, no Wine.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

import shimmer_trace

ROOT = Path(__file__).resolve().parents[2]
FADE_PAIRS = {('b0602757fce6e870', '517540ae6d5e5410'),
              ('0c223ad11bce02d5', '7a0c3388065bb08d'),
              ('233d17d26ce0c1fc', '7a0c3388065bb08d'),
              ('167eb2d5629ab9d3', 'd44db87778a43b61'),
              ('330ceb9dd874ede2', '550c2a4d4d3ed70f'),
              ('12b8a13f13fe8cfe', '550c2a4d4d3ed70f')}
# Fade admission (linear_distance_fade_pair): the six plus the station hull pair
# (docs/architecture/linear-station-source-over.md); never Asteroid-class.
STATION_PAIR = ('4944d81dfe531b37', '64bac8bb307eb896')


def frame_line(frame, *, asteroid=0, logged=0, truncated=0, taa_history=1, taa_skip=0,
               cut=0, jitter_index=3, p00=8000, p11=14000, draws=900):
    return (f'shimmer_frame device=1 frame={frame} draws={draws} asteroid={asteroid} logged={logged}'
            f' truncated={truncated} taa=1 taa_attempted=1 taa_resolved=1 taa_history={taa_history}'
            f' taa_skip={taa_skip} cut={cut} camera_cut=0 jitter=1 jitter_index={jitter_index}'
            f' history_previous=1 history_current=1 committed=1 camera_valid=1'
            f' p00_e4={p00} p11_e4={p11}')


def draw_line(frame, index, node, *, model=0x11, lod=0, vertex_count=120, index_count=300,
              primitives=100, f_permille=7, region=1, rect='10,20,60,70', routed=1, composition=1):
    return (f'shimmer_draw device=1 frame={frame} index={index} gate=0 routed={routed}'
            f' composition={composition} node={node} model={model:08x} lod={lod:08x} vb=41 ib=42'
            f' topology=4 indexed=1 vertex_count={vertex_count} index_count={index_count}'
            f' primitives={primitives} f_permille={f_permille} region={region} rect={rect}')


class ShimmerTraceParser(unittest.TestCase):
    def test_frame_and_draw_fields(self):
        lines = [frame_line(10, asteroid=2, logged=2),
                 draw_line(10, 41, 0x5000),
                 draw_line(10, 44, 0x6000, lod=2, f_permille=-1, region=0, rect='0,0,1,1')]
        frames = shimmer_trace.parse(lines)
        self.assertEqual(len(frames), 1)
        f = frames[0]
        self.assertEqual((f.frame, f.asteroid, f.logged, f.truncated), (10, 2, 2, 0))
        self.assertAlmostEqual(f.p00, 0.8)
        self.assertAlmostEqual(f.p11, 1.4)
        self.assertEqual(f.taa_history, 1)
        self.assertFalse(f.history_dropped)
        first, second = f.draws_logged
        self.assertEqual((first.node, first.model, first.lod), (0x5000, 0x11, 0))
        self.assertEqual((first.vertex_count, first.index_count, first.primitives), (120, 300, 100))
        self.assertTrue(first.fade_admitted)
        self.assertAlmostEqual(first.f, 0.007)
        self.assertEqual(first.rect, (10, 20, 60, 70))
        self.assertTrue(first.region_known)
        self.assertFalse(second.fade_admitted)
        self.assertIsNone(second.f)
        self.assertFalse(second.region_known)

    def test_truncation_count(self):
        lines = [frame_line(11, asteroid=35, logged=32, truncated=3)]
        lines += [draw_line(11, i, 0x7000 + i) for i in range(32)]
        f = shimmer_trace.parse(lines)[0]
        self.assertEqual(f.truncated, 3)
        self.assertEqual(f.asteroid - f.logged, 3)
        self.assertEqual(len(f.draws_logged), 32)

    def test_lod_change_and_disappearance_between_frames(self):
        lines = [frame_line(20, asteroid=2, logged=2),
                 draw_line(20, 1, 0x5000, lod=1),
                 draw_line(20, 2, 0x6000, lod=1),
                 frame_line(21, asteroid=2, logged=2, taa_history=0),
                 draw_line(21, 1, 0x5000, lod=2),
                 draw_line(21, 3, 0x7000, lod=0)]
        before, after = shimmer_trace.parse(lines)
        self.assertEqual(before.lod_changes, [])
        self.assertEqual(after.lod_changes, [((0x5000, 0x11), 1, 2)])
        self.assertEqual(after.disappeared, [(0x6000, 0x11)])
        self.assertEqual(after.appeared, [(0x7000, 0x11)])
        self.assertTrue(after.history_dropped)

    def test_non_consecutive_frames_are_not_compared(self):
        lines = [frame_line(30, asteroid=1, logged=1), draw_line(30, 1, 0x5000, lod=1),
                 frame_line(60, asteroid=1, logged=1), draw_line(60, 1, 0x5000, lod=3)]
        _, later = shimmer_trace.parse(lines)
        self.assertEqual(later.lod_changes, [])
        self.assertEqual(later.disappeared, [])

    def test_unresolved_identities_are_not_tracked(self):
        # Gate-failed draws log node=0 model=00000000; tracking them would
        # collapse every one into a single identity and fabricate changes.
        lines = [frame_line(50, asteroid=3, logged=3),
                 draw_line(50, 1, 0x5000, lod=1),
                 draw_line(50, 2, 0, model=0, lod=0, routed=0, composition=0),
                 draw_line(50, 3, 0, model=0, lod=1, routed=0, composition=0),
                 frame_line(51, asteroid=3, logged=3),
                 draw_line(51, 1, 0x5000, lod=1),
                 draw_line(51, 2, 0, model=0, lod=2, routed=0, composition=0),
                 draw_line(51, 3, 0, model=0, lod=3, routed=0, composition=0)]
        before, after = shimmer_trace.parse(lines)
        self.assertEqual(before.unidentified, 2)
        self.assertEqual(after.unidentified, 2)
        self.assertEqual(after.lod_changes, [])
        self.assertEqual(after.appeared, [])
        self.assertEqual(after.disappeared, [])

    def test_other_lines_are_ignored(self):
        lines = ['fade_region device=1 frame=40 index=3 bound=1',
                 frame_line(40, asteroid=1, logged=1),
                 'motion_output_frame device=1 frame=40 draws=900',
                 draw_line(40, 5, 0x5000)]
        frames = shimmer_trace.parse(lines)
        self.assertEqual(len(frames), 1)
        self.assertEqual(len(frames[0].draws_logged), 1)



class AsteroidPairTable(unittest.TestCase):
    """Exactly the six Asteroid pairs are Asteroid-class; fade admits them plus the station pair."""

    def table_pairs(self):
        source = (ROOT / 'src/renderer/linear_material.cpp').read_text()
        block = source.split('constexpr Pair pairs[] = {', 1)[1].split('\n};', 1)[0]
        return re.findall(r'\{0x([0-9a-f]+)ull,0x([0-9a-f]+)ull', block)

    def test_exactly_the_six_fade_pairs(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise unittest.SkipTest('A host C++ compiler is required')
        pairs = self.table_pairs()
        self.assertGreater(len(pairs), 100)
        self.assertTrue(FADE_PAIRS.issubset(set(pairs)))
        with tempfile.TemporaryDirectory(prefix='x3-asteroid-pairs-') as directory:
            executable = Path(directory) / 'asteroid_pair_table'
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/asteroid_pair_table.cpp'),
                                    str(ROOT / 'src/renderer/linear_material.cpp'),
                                    str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True)
            if build.returncode:
                raise AssertionError(build.stdout + build.stderr)
            run = subprocess.run([str(executable)], input=''.join(f'{v} {p}\n' for v, p in pairs),
                                 capture_output=True, text=True, check=True)
        asteroid, fade = set(), set()
        for line in run.stdout.splitlines():
            vs, ps, flag, mask = line.split()
            if flag == 'asteroid=1':
                asteroid.add((vs, ps))
            if mask != 'fade=0':
                fade.add((vs, ps))
        self.assertEqual(asteroid, FADE_PAIRS)
        self.assertEqual(fade, FADE_PAIRS | {STATION_PAIR})


if __name__ == '__main__':
    unittest.main()
