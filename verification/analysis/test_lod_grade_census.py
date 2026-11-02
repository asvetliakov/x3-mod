"""lod_grade_census: merged materials whose grade differs from an absorbed material's (2026-11-03, FL green station)."""
import contextlib
import copy
import gzip
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import bob1
import lod_grade_census
from sector_fog_census import write_catalogue

VENT = [(b'Brightness', 2, [67502]), (b'saturation', 2, [472514]), (b'hue', 2, [14278459]), (b'colormatrix', 1, [1])]
PLAIN = [(b'Brightness', 2, [65536]), (b'saturation', 2, [65536]), (b'hue', 2, [0]), (b'colormatrix', 1, [0])]
MEMBER = 'objects/ships/x/b.pbb'


def mat(diff, grade):
    return {'index': 0, 'flags': 0x02000000, 'technique': 1, 'effect': b'argon.fx',
            'params': [(b't_DiffuseTexture', 8, diff), (b't_LightMapTexture', 8, b'NULL')] + grade}


def body(mats):
    pt = lambda x, y: (0x1b, x, y, 0, 0, 0, 0, 0, 65536, 1)
    rec = {'value': 5, 'flags': 0, 'points': [pt(0, 0), pt(10, 0), pt(0, 10)],
           'parts': [{'flags': 1, 'groups': [{'material': 0, 'faces': [(0, 1, 2, 1)]}]}]}
    return gzip.compress(bob1.serialise({'sections': [('MAT6', mats), ('BODY', [rec])]}), mtime=0)


class GradeCensus(unittest.TestCase):
    def bake(self, folder, merged, synth, src=None):
        """Game (02.cat: the vent + plain source body, or `src`) and candidate OUT/addon/16.cat with `merged`
        appended."""
        src = src or [mat(b'a.tga', VENT), mat(b'b.tga', PLAIN)]
        game, out = Path(folder) / 'game', Path(folder) / 'out'
        write_catalogue(game / '01.cat', [('t/x.txt', b'x')])                 # catalogues mount from 01 on
        write_catalogue(game / '02.cat', [(MEMBER, body(src))])
        write_catalogue(out / 'addon/16.cat', [(MEMBER, body(src + merged))])
        marker = {'bodies': [{'name': 'ships/x/b', 'source': '02.cat', 'member': MEMBER, 'collapse': 'atlas',
                              'atlas': {'tiles': []}, 'synth': synth}]}
        (out / 'addon/16.x3m-lod.json').write_text(json.dumps(marker))
        return game, out, src

    def run_census(self, game, out):
        text = io.StringIO()
        with contextlib.redirect_stdout(text):
            self.assertEqual(lod_grade_census.main(['--game', str(game), '--edition', 'ap', '--candidate', str(out)]), 0)
        lines = text.getvalue().splitlines()
        return json.loads(lines[-1].removeprefix('TOTAL ')), lines[:-1]

    def test_spread_grade_is_a_mismatch(self):
        """One merged copy of the graded dominant over both materials (the pre-fix bake): one mismatch body, the
        plain material named, one extra draw for the split."""
        with tempfile.TemporaryDirectory() as folder:
            game, out, src = self.bake(folder, [copy.deepcopy(mat(b'x.tga', VENT))],
                                       [{'index': 2, 'dominant': 0, 'absorbed': [0, 1], 'atlas': True}])
            tot, rows = self.run_census(game, out)
            self.assertEqual((tot['mismatch_bodies'], tot['mismatch_rows'], tot['dominant_graded_spread']), (1, 1, 1))
            self.assertEqual((tot['extra_draws'], tot['split_bodies'], tot['extra_per_body'], tot['extra_max']),
                             (1, 1, {'1': 1}, [1, 'ships/x/b']))
            self.assertEqual((tot['merged_materials'], tot['merged_materials_split'], tot['unread']), (1, 2, 0))
            self.assertIn('wrong=[1] source=baked', rows[0])
            ships = tot['per_class']['ships']
            self.assertEqual((ships['bodies'], ships['affected'], ships['merged_before'], ships['merged_after'],
                              ships['hist'], ships['max'], ships['affected_only']['after_median']),
                             (1, 1, 1, 2, {'1': 1}, [1, 'ships/x/b'], 2))
            self.assertEqual(list(tot['per_class']), ['ships'])

    def test_split_bake_has_no_mismatch(self):
        """A merged material per grade (the fixed bake): no mismatch, no extra draw."""
        with tempfile.TemporaryDirectory() as folder:
            game, out, _ = self.bake(folder, [mat(b'x.tga', VENT), mat(b'x.tga', PLAIN)],
                                     [{'index': 2, 'dominant': 0, 'absorbed': [0], 'atlas': True},
                                      {'index': 3, 'dominant': 1, 'absorbed': [1], 'atlas': True}])
            tot, rows = self.run_census(game, out)
            self.assertEqual((tot['mismatch_bodies'], tot['extra_draws'], tot['atlas_rows'], rows), (0, 0, 2, []))

    def test_palette_counts_only_with_mixing(self):
        """Two palettes over one merged material: no mismatch with g_bColor_Mixing 0, a mismatch with it 1."""
        for mixing, wrong in ((0, 0), (1, 1)):
            pal = lambda c: PLAIN + [(b'g_bColor_Mixing', 1, [mixing]), (b'g_Color_1', 5, [c, c, c, 0])]
            src = [mat(b'a.tga', pal(100)), mat(b'b.tga', pal(200))]
            with tempfile.TemporaryDirectory() as folder:
                game, out, _ = self.bake(folder, [mat(b'x.tga', pal(100))],
                                         [{'index': 2, 'dominant': 0, 'absorbed': [0, 1], 'atlas': True}], src)
                tot, _ = self.run_census(game, out)
                self.assertEqual((tot['mismatch_bodies'], tot['extra_draws'], tot['extra_draws_colormatrix_family']),
                                 (wrong, wrong, 0))
                self.assertEqual((tot['mixing_on'], tot['mixing_off']), (2, 0) if mixing else (0, 2))   # source records


if __name__ == '__main__':
    unittest.main()
