"""Star-texture marks of the sky row group (tools/analysis/sky_textures.py; docs/architecture/sector-background-modern.md,
answer 1) on synthetic inputs: the X3ML payload-2 mark with texels byte-identical, its round trip through the proxy's
own parser (src/proxy/lod_texture_mark.h), and the overlay batch's sky group (lod_overlay.py --batch --sky): members in
the same slot, the marker's sky rows, reuse keyed on inputs_sha256 independently of the bodies, the selected package's
derived copy, the FL addon2 layout, --budget-bytes, the candidate check, and the removal of the nebula part members an
earlier version wrote. No game bytes."""
import gzip
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import lod_atlas  # noqa: E402
import lod_overlay  # noqa: E402
import lod_overlay_check  # noqa: E402
import sky_textures as st  # noqa: E402
from sector_fog_census import unpack, write_catalogue  # noqa: E402
from test_lod_overlay_batch import BATCH, cat_members, make_game, run  # noqa: E402

PART = 'dds/nebula_testfam_background_part_01_diff.pck'
STARS = 'dds/nebula_testfam_stars_blue_diff.pck'


def star_texture(n=64):
    """DXT1 DDS with 11 bright 2x2 stars (luma > 96) on black and a full mip chain."""
    img = np.zeros((n, n, 4), np.uint8)
    img[..., 3] = 255
    for i in range(11):
        y, x = 4 + (i * 13) % (n - 8), 4 + (i * 29) % (n - 8)
        img[y:y + 2, x:x + 2, :3] = (200, 210, 255)
    return lod_atlas.write_dds(lod_atlas.mip_chain(img.astype(np.float64)), 'DXT1')


def nebula_part(n=64):
    img = np.full((n, 2 * n, 4), 255, np.uint8)
    img[..., :3] = (20, 30, 60)
    return lod_atlas.write_dds([img], 'DXT1')


class Marks(unittest.TestCase):
    CHECK = r'''
#include "lod_texture_mark.h"
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        FILE* f = std::fopen(argv[i], "rb");
        std::vector<unsigned char> d;
        int c;
        while ((c = std::fgetc(f)) != EOF) d.push_back((unsigned char)c);
        std::fclose(f);
        x3m::lod_texture_mark::Mark m;
        bool ok = x3m::lod_texture_mark::parse(d.data(), d.size(), m);
        std::printf("%d %u %u %u %u %u %u\n", ok ? 1 : 0, m.version, m.slot, m.lane, m.k, m.payload,
                    (unsigned)x3m::lod_texture_mark::lane_of(m));
    }
    return 0;
}
'''

    def test_star_mark_and_identical_texels(self):
        src = star_texture()
        dds, info = st.bake_stars(src, 'bluewell', 'blue')
        self.assertEqual(dds[:32], src[:32])
        self.assertEqual(dds[44:], src[44:])                                 # texels and every mip byte-identical
        self.assertEqual((info['slot'], info['lane'], info['k'], info['payload']),
                         (st.fnv1a32(b'bluewell') & 255, 1, info['peaks'] // 8, 2))
        self.assertGreaterEqual(info['peaks'], 11)
        self.assertEqual(st.star_descriptor('x', 'mauve', lod_atlas.decode_dds(src).astype(np.float32))[1], st.LANE_OTHER)

    def test_round_trip_through_the_proxy_parser(self):
        """Payload 2 through lod_texture_mark.h: the header constants and the parser's own decode."""
        header = (ROOT / 'src/proxy/lod_texture_mark.h').read_text()
        magic = int(re.search(r'magic = 0x([0-9a-f]+)u', header).group(1), 16)
        self.assertEqual(struct.pack('<I', magic), lod_atlas.DDS_MARK)
        self.assertEqual(int(re.search(r'mark_offset = (\d+)', header).group(1)), lod_atlas.DDS_MARK_OFFSET)
        self.assertEqual(int(re.search(r'version = (\d+);', header).group(1)), lod_atlas.DDS_MARK_VERSION)
        stars, sinfo = st.bake_stars(star_texture(), 'avdustysol', 'orange')
        want = (1, 1, sinfo['slot'], 3, sinfo['k'], 2, 0)
        self.assertEqual(lod_atlas.read_dds_mark(stars), want[1:6])
        compiler = shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            self.skipTest('host C++ compiler required for the parser round trip')
        with tempfile.TemporaryDirectory(prefix='x3-sky-mark-') as folder:
            f = Path(folder)
            (f / 'check.cpp').write_text(self.CHECK)
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I',
                                    str(ROOT / 'src/proxy'), str(f / 'check.cpp'), '-o', str(f / 'check')],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            (f / 'stars.dds').write_bytes(stars)
            out = subprocess.run([str(f / 'check'), str(f / 'stars.dds')], capture_output=True, text=True,
                                 check=True).stdout.split('\n')
        self.assertEqual(tuple(int(v) for v in out[0].split()), want)   # lane_of 0: the strength lane untouched

    def test_names(self):
        self.assertEqual(st.classify('dds\\nebula_red_reef_stars_orange_diff.pck'), ('red_reef', 'orange'))
        self.assertEqual(st.classify('addon2/dds/nebula_bluewell_stars_blue_diff.pck'), ('bluewell', 'blue'))
        self.assertIsNone(st.classify(PART))                                 # nebula parts are not sky members
        self.assertIsNone(st.classify('dds/nebula_bluewell_dust_diff.pck'))


def sky_game(folder):
    game = make_game(folder)
    write_catalogue(game / '04.cat', [(PART, gzip.compress(nebula_part(), mtime=0)),
                                      (STARS, gzip.compress(star_texture(), mtime=0))])
    return game


def changed_body():
    from test_lod_overlay_batch import atlas_tree_lod0, packed, with_threshold
    return packed(with_threshold(atlas_tree_lod0(), 6))


def install(out, game, slot=2, xp='addon'):
    for ext in ('cat', 'dat', 'x3m-lod.json'):
        shutil.copy(out / xp / f'{slot:02d}.{ext}', game / xp / f'{slot:02d}.{ext}')


@unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
class OverlayGroup(unittest.TestCase):
    def test_members_marker_and_reuse(self, _running):
        """--sky on: the marked star texture rides in the overlay slot under its stock name with a marker sky group (the
        nebula part stays stock); --sync reuses it by inputs_sha256; a sky recipe change re-marks it and rebakes no
        body; a body change rebakes that body and re-marks nothing; the candidate check counts the member as a sky
        override, not a collision; --sky auto on AP writes no group."""
        sky = BATCH + ['--sky', 'on']
        with tempfile.TemporaryDirectory() as folder:
            game = sky_game(folder)
            out1 = Path(folder) / 'out1'
            code, text = run(sky + ['--game', str(game), '--out', str(out1)])
            self.assertEqual(code, 0)
            self.assertIn('1 star textures marked in 1 families, built 1 + reused 0', text)
            marker = json.loads((out1 / 'addon/02.x3m-lod.json').read_text())
            g = marker['sky']
            self.assertEqual((g['recipe_id'], g['tool_sha256']), (st.RECIPE_ID, st.tool_sha256()))
            self.assertNotIn('sky_textures.py', lod_overlay.TOOL_FILES)
            both = (ROOT / 'tools/analysis/sky_textures.py').read_bytes() + (ROOT / 'tools/analysis/lod_atlas.py').read_bytes()
            self.assertEqual(g['tool_sha256'], lod_overlay.hashlib.sha256(both).hexdigest())
            self.assertEqual([r['member'] for r in g['rows']], [STARS])
            members = cat_members(out1 / 'addon/02.cat')
            self.assertNotIn(PART, members)
            self.assertEqual(lod_overlay.hashlib.sha256(members[STARS]).hexdigest(), g['rows'][0]['member_sha256'])
            self.assertEqual(unpack(members[STARS])[44:], star_texture()[44:])
            self.assertEqual(lod_atlas.read_dds_mark(unpack(members[STARS]))[4], st.PAYLOAD_SKY_STARS)
            record = json.loads((out1 / 'x3m-lod-batch.json').read_text())
            self.assertEqual((record['sky']['families']['testfam']['stars'], record['slots'][0]['sky']), (1, 1))
            cand = lod_overlay_check.candidate_state(game, out1)
            self.assertEqual(cand['state'], 'ok', cand['clauses'])
            self.assertTrue(any('1 sky texture members override' in c for c in cand['clauses']))
            install(out1, game)
            out2 = Path(folder) / 'out2'
            code, text = run(sky + ['--sync', '--game', str(game), '--out', str(out2)])
            self.assertIn('built 0 + reused 9 = 9', text)
            self.assertIn('built 0 + reused 1', text)
            self.assertEqual(cat_members(out2 / 'addon/02.cat'), members)
            out3 = Path(folder) / 'out3'
            with unittest.mock.patch.object(st, 'RECIPE_ID', 'sky-test-recipe'):
                code, text = run(sky + ['--sync', '--game', str(game), '--out', str(out3)])
            self.assertIn('built 0 + reused 9 = 9', text)
            self.assertIn('built 1 + reused 0', text)
            write_catalogue(game / '03.cat', [('objects/ships/x/good.pbb', changed_body())])
            out4 = Path(folder) / 'out4'
            code, text = run(sky + ['--sync', '--game', str(game), '--out', str(out4)])
            self.assertIn('built 1 + reused 8 = 9', text)
            self.assertIn('built 0 + reused 1', text)
            out5 = Path(folder) / 'out5'
            code, text = run(BATCH + ['--game', str(game), '--out', str(out5)])
            self.assertIn('sky group off (--sky auto, auto: off on AP)', text)
            self.assertNotIn('sky', json.loads((out5 / 'addon/02.x3m-lod.json').read_text()))
            self.assertNotIn(STARS, cat_members(out5 / 'addon/02.cat'))

    def test_sync_drops_part_members_of_an_earlier_version(self, _running):
        """An installed overlay that an earlier version wrote with a nebula part member and its sky row (FL run 49):
        --sync keeps the star member (reused, payload 2) and writes neither the part member nor its row."""
        with tempfile.TemporaryDirectory() as folder:
            game = sky_game(folder)
            out1 = Path(folder) / 'out1'
            run(BATCH + ['--sky', 'on', '--game', str(game), '--out', str(out1)])
            cat = out1 / 'addon/02.cat'
            members = cat_members(cat)
            old_part = gzip.compress(nebula_part(128), mtime=0)
            write_catalogue(cat, list(members.items()) + [(PART, old_part)])
            marker_path = out1 / 'addon/02.x3m-lod.json'
            marker = json.loads(marker_path.read_text())
            marker['sky']['rows'].append(dict(member=PART, source='04.cat', inputs_sha256='earlier',
                                              member_sha256=lod_overlay.hashlib.sha256(old_part).hexdigest(),
                                              metrics=dict(kind='part', family='testfam')))
            h = lod_overlay.hash_files([cat, cat.with_suffix('.dat')])
            marker['overlay_sha256'] = {'cat': h[str(cat)], 'dat': h[str(cat.with_suffix('.dat'))]}
            marker['overlay_dat_bytes'] = cat.with_suffix('.dat').stat().st_size
            marker_path.write_text(json.dumps(marker, indent=1) + '\n')
            install(out1, game)
            self.assertEqual([m['status'] for m in lod_overlay.installed_markers(game)], ['valid'])
            self.assertIn(PART, cat_members(game / 'addon/02.cat'))
            out2 = Path(folder) / 'out2'
            code, text = run(BATCH + ['--sky', 'on', '--sync', '--game', str(game), '--out', str(out2)])
            self.assertEqual(code, 0)
            self.assertIn('built 0 + reused 1', text)
            after = cat_members(out2 / 'addon/02.cat')
            self.assertNotIn(PART, after)
            self.assertEqual([r['member'] for r in json.loads((out2 / 'addon/02.x3m-lod.json').read_text())['sky']['rows']],
                             [STARS])
            self.assertEqual(lod_atlas.read_dds_mark(unpack(after[STARS]))[4], st.PAYLOAD_SKY_STARS)
            self.assertEqual(unpack(after[STARS])[44:], star_texture()[44:])

    def test_selected_package_star_texture_goes_into_the_derived_copy(self, _running):
        """--mod NAME: a star texture the package overrides is marked into the derived <mod>-x3m-lod copy (replacing the
        package's member once); the numbered overlay keeps the stock-view row; nothing raises."""
        from test_lod_overlay_batch import PACKAGE
        with tempfile.TemporaryDirectory() as folder:
            game = sky_game(folder)
            mod_stars = star_texture(32)
            write_catalogue(game / 'addon/mods/Big.cat', PACKAGE + [(STARS, gzip.compress(mod_stars, mtime=0))])
            out = Path(folder) / 'out'
            code, text = run(BATCH + ['--sky', 'on', '--mod', 'Big', '--game', str(game), '--out', str(out)])
            self.assertEqual(code, 0)
            self.assertIn('package Big: 1 textures, built 1 + reused 0, into addon/mods/Big-x3m-lod', text)
            copy = cat_members(out / 'addon/mods/Big-x3m-lod.cat')
            self.assertEqual(sum(1 for p in copy if p.lower() == STARS), 1)
            star = unpack(copy[STARS])
            self.assertEqual(star[44:], mod_stars[44:])
            self.assertEqual(lod_atlas.read_dds_mark(star)[4], st.PAYLOAD_SKY_STARS)
            derived = json.loads((out / 'addon/mods/Big-x3m-lod.x3m-lod.json').read_text())
            self.assertEqual([r['member'] for r in derived['sky']['rows']], [STARS])
            numbered = json.loads((out / 'addon/02.x3m-lod.json').read_text())
            self.assertEqual([r['member'] for r in numbered['sky']['rows']], [STARS])

    def test_fl_layout_member_in_addon2(self, _running):
        """FL (addon2 slots, --sky auto = on): the star texture lands in addon2/NN with its marker sky row."""
        from test_lod_overlay_batch import make_fl_game
        with tempfile.TemporaryDirectory() as folder:
            game, out = make_fl_game(folder), Path(folder) / 'out'
            (game / 'addon2/03.cat').unlink()
            (game / 'addon2/03.dat').unlink()
            write_catalogue(game / '03.cat', [(STARS, gzip.compress(star_texture(), mtime=0)),
                                              (PART, gzip.compress(nebula_part(), mtime=0))])
            code, text = run(BATCH + ['--game', str(game), '--out', str(out), '--mod', 'none'])
            self.assertEqual(code, 0, text)
            marker = json.loads((out / 'addon2/02.x3m-lod.json').read_text())
            self.assertEqual(([r['member'] for r in marker['sky']['rows']], marker['edition']), ([STARS], 'fl'))
            members = cat_members(out / 'addon2/02.cat')
            self.assertEqual(lod_atlas.read_dds_mark(unpack(members[STARS]))[4], st.PAYLOAD_SKY_STARS)
            self.assertNotIn(PART, members)
            self.assertFalse((out / 'addon').exists())

    def test_budget_keeps_textures(self, _running):
        with tempfile.TemporaryDirectory() as folder:
            game = sky_game(folder)
            out = Path(folder) / 'out'
            code, text = run(BATCH + ['--sky', 'on', '--budget-bytes', '1', '--game', str(game), '--out', str(out)])
            self.assertEqual(code, 0)
            self.assertIn('the sky textures', text)
            marker = json.loads((out / 'addon/02.x3m-lod.json').read_text())
            self.assertEqual(marker['bodies'], [])
            self.assertEqual(len(marker['sky']['rows']), 1)


if __name__ == '__main__':
    unittest.main()
