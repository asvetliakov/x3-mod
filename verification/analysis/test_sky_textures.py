"""Star-texture marks of the sky row group (tools/analysis/sky_textures.py; docs/architecture/sector-background-modern.md,
answer 1) on synthetic inputs: the X3ML payload-2 mark with texels byte-identical, its round trip through the proxy's
own parser (src/proxy/lod_texture_mark.h), and the overlay batch's sky group (lod_overlay.py --batch --sky): members in
the same slot, the marker's sky rows, reuse keyed on inputs_sha256 independently of the bodies, the selected package's
derived copy, the FL addon2 layout, --budget-bytes, the candidate check, the removal of the nebula part members an
earlier version wrote, and loose star textures marked in place by the regenerate sky step (reuse, re-mark on a
mod update, restore with the group off, refusals). No game bytes."""
import gzip
import json
import os
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
        # no colour token (the Mayhem/Litcube nebula_litcube<N>_stars_diff): the family alone, lane 7
        self.assertEqual(st.classify('dds/nebula_litcube7_stars_diff.dds'), ('litcube7', None))
        self.assertEqual(st.classify('addon/dds/nebula_zz_stars_diff.pck'), ('zz', None))
        self.assertEqual(st.classify('dds/nebula_a_stars_b_stars_c_diff.dds'), ('a_stars_b', 'c'))
        self.assertEqual(st.classify('dds/nebula_brennanstriumph_stars_redfog_diff.dds'), ('brennanstriumph', 'redfog'))
        # any stem with nebula and stars (any case, any position): family without nebula/stars/diff and the colour
        self.assertEqual(st.classify('DDS/X_Nebula_Foo_Stars.dds'), ('x_foo', None))
        self.assertEqual(st.classify('dds/x_nebula__red__stars_glow.pck'), ('x_glow', 'red'))
        self.assertEqual(st.classify('dds/nebula_stars_diff.dds'), ('nebula_stars_diff', None))
        self.assertEqual(st.classify('dds/nebula_x_stars__diff.dds'), ('x', None))
        self.assertIsNone(st.classify('dds/nebula_x_stars_diff.tga'))
        self.assertIsNone(st.classify('textures/nebula_x_stars_diff.dds'))
        self.assertIsNone(st.classify('dds/nebula_x_dust_diff.dds'))

    def test_colour_token_bytes_unchanged(self):
        """The marked bytes of colour-token inputs are those of the sky-stars-1 recipe before the no-token rule."""
        for src, family, colour, want in ((star_texture(), 'testfam', 'blue', '491b8740bf50fc64'),
                                          (loose_stars(), 'loosefam', 'red', '0bf0ad616a436c05'),
                                          (loose_stars(), 'red_reef', 'orange', 'c9fc2fcd39c10df5')):
            dds, _ = st.bake_stars(src, family, colour)
            self.assertEqual(lod_overlay.hashlib.sha256(dds).hexdigest()[:16], want)
        self.assertEqual(st.bake_stars(star_texture(), 'x', None)[1]['lane'], st.LANE_OTHER)
        self.assertRaises(st.NotStarBody, st.bake_stars, nebula_part(), 'x', None)   # 0 % below luma 16


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
        override, not a collision; --sky auto on AP writes the group like --sky on, --sky off writes none."""
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
            self.assertEqual(code, 0, text)
            self.assertIn('1 star textures marked in 1 families', text)
            auto = json.loads((out5 / 'addon/02.x3m-lod.json').read_text())
            self.assertEqual([r['member'] for r in auto['sky']['rows']], [STARS])
            self.assertEqual(lod_atlas.read_dds_mark(unpack(cat_members(out5 / 'addon/02.cat')[STARS]))[4],
                             st.PAYLOAD_SKY_STARS)
            out6 = Path(folder) / 'out6'
            code, text = run(BATCH + ['--sky', 'off', '--game', str(game), '--out', str(out6)])
            self.assertIn('sky group off (--sky off)', text)
            self.assertNotIn('sky', json.loads((out6 / 'addon/02.x3m-lod.json').read_text()))
            self.assertNotIn(STARS, cat_members(out6 / 'addon/02.cat'))

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


LOOSE = 'addon/dds/nebula_loosefam_stars_red_diff.dds'


def loose_stars(shift=0, n=64):
    """A loose star texture (DXT1, 17 stars, full mips) unlike star_texture(); shift moves the stars (a mod update)."""
    img = np.zeros((n, n, 4), np.uint8)
    img[..., 3] = 255
    for i in range(17):
        y, x = 3 + (i * 7 + shift) % (n - 6), 3 + (i * 19 + 2 * shift) % (n - 6)
        img[y:y + 2, x:x + 2, :3] = (255, 120, 90)
    return lod_atlas.write_dds(lod_atlas.mip_chain(img.astype(np.float64)), 'DXT1')


def sky_step(game):
    sys.path.insert(0, str(ROOT / 'tools' / 'regenerate'))
    import io
    import x3m_regenerate as regen
    console = io.StringIO()
    log = regen.Log(Path(game) / 'sky.log', console)
    try:
        regen.sky_step(log, Path(game))
    finally:
        log.close()
    return console.getvalue()


@unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
class LooseStars(unittest.TestCase):
    def test_marked_in_place_reused_remarked_restored(self, _running):
        """One catalogue and one loose star texture: the overlay carries the catalogue one; the regenerate sky step
        marks the loose file in its own header (payload 2, texels unchanged), records the original header; a rerun
        writes nothing (reused); a mod update re-marks it with the new header as the original; --sky off restores
        the recorded bytes exactly and removes the ledger."""
        with tempfile.TemporaryDirectory() as folder:
            game = sky_game(folder)
            f = game / LOOSE
            f.parent.mkdir(parents=True)
            original = loose_stars()
            f.write_bytes(original)
            code, text = run(BATCH + ['--sky', 'on', '--game', str(game), '--install'])
            self.assertEqual(code, 0, text)
            self.assertIn('skipped loose_file 1', text)                      # the overlay cannot carry it
            out = sky_step(game)
            self.assertIn('sky textures family=loosefam stars=1 marked built=1 reused=0 loose=1', out)
            self.assertIn('sky textures family=testfam stars=1 marked built=1 reused=0\n', out)
            self.assertIn('2 star textures marked, 2 new + 0 unchanged', out)
            self.assertIn('marked in-place 1 loose; skipped none', out)
            marked = f.read_bytes()
            self.assertEqual((marked[:32], marked[44:]), (original[:32], original[44:]))   # texels and mips
            mark = lod_atlas.read_dds_mark(marked)
            _, want = st.bake_stars(original, 'loosefam', 'red')
            self.assertEqual(mark, (1, want['slot'], want['lane'], want['k'], st.PAYLOAD_SKY_STARS))
            self.assertEqual(mark[1:3], (st.fnv1a32(b'loosefam') & 255, st.STAR_LANES.index('red')))
            ledger = json.loads(st.ledger_path(game).read_text())
            row, = ledger['rows']
            self.assertEqual((row['loose'], row['original_header']), (LOOSE, original[:128].hex()))
            self.assertEqual((row['original_sha256'], row['marked_sha256']),
                             (lod_overlay.hashlib.sha256(original).hexdigest(),
                              lod_overlay.hashlib.sha256(marked).hexdigest()))
            mtime = f.stat().st_mtime_ns
            out = sky_step(game)                                             # rerun: nothing written
            self.assertIn('family=loosefam stars=1 marked built=0 reused=1 loose=1', out)
            self.assertEqual((f.read_bytes(), f.stat().st_mtime_ns), (marked, mtime))
            update = loose_stars(shift=5)                                    # a mod update replaces the file
            f.write_bytes(update)
            out = sky_step(game)
            self.assertIn('family=loosefam stars=1 marked built=1 reused=0 loose=1', out)
            remarked = f.read_bytes()
            self.assertEqual((remarked[:32], remarked[44:]), (update[:32], update[44:]))
            self.assertEqual(lod_atlas.read_dds_mark(remarked)[4], st.PAYLOAD_SKY_STARS)
            self.assertEqual(json.loads(st.ledger_path(game).read_text())['rows'][0]['original_header'],
                             update[:128].hex())
            code, text = run(BATCH + ['--sky', 'off', '--sync', '--game', str(game), '--install'])
            self.assertEqual(code, 0, text)
            out = sky_step(game)
            self.assertIn(f'sky texture {LOOSE}: restored (loose file, the sky group is off)', out)
            self.assertEqual(f.read_bytes(), update)                         # the original bytes exactly
            self.assertFalse(st.ledger_path(game).exists())

    def test_no_colour_token_loose_and_catalogue(self, _running):
        """A loose AP dds/nebula_litcube7_stars_diff.dds is marked in place and a catalogue member
        dds/nebula_zz_stars_diff.pck rides in the overlay: lane 7, slot fnv1a32(family) & 0xff, distinct slots."""
        zz, lit = 'dds/nebula_zz_stars_diff.pck', 'dds/nebula_litcube7_stars_diff.dds'
        with tempfile.TemporaryDirectory() as folder:
            game = make_game(folder)
            write_catalogue(game / '04.cat', [(STARS, gzip.compress(star_texture(), mtime=0)),
                                              (zz, gzip.compress(star_texture(), mtime=0))])
            f = game / lit
            f.parent.mkdir(parents=True)
            original = loose_stars()
            f.write_bytes(original)
            code, text = run(BATCH + ['--sky', 'on', '--game', str(game), '--install'])
            self.assertEqual(code, 0, text)
            self.assertIn('2 star textures marked in 2 families', text)
            members = cat_members(game / 'addon/02.cat')
            zmark = lod_atlas.read_dds_mark(unpack(members[zz]))
            self.assertEqual(unpack(members[zz])[44:], star_texture()[44:])
            out = sky_step(game)
            self.assertIn('sky textures family=litcube7 stars=1 marked built=1 reused=0 loose=1', out)
            self.assertIn('sky textures family=zz stars=1 marked built=1 reused=0\n', out)
            marked = f.read_bytes()
            self.assertEqual((marked[:32], marked[44:]), (original[:32], original[44:]))
            lmark = lod_atlas.read_dds_mark(marked)
            for mark, family in ((zmark, b'zz'), (lmark, b'litcube7')):
                self.assertEqual(mark[1:3], (st.fnv1a32(family) & 255, st.LANE_OTHER))
                self.assertEqual(mark[4], st.PAYLOAD_SKY_STARS)
            self.assertEqual(lmark[3], st.bake_stars(original, 'litcube7', None)[1]['k'])
            self.assertNotEqual(zmark[1], lmark[1])
            self.assertIsNone(json.loads(st.ledger_path(game).read_text())['rows'][0]['colour'])
            self.assertIn('family=litcube7 stars=1 marked built=0 reused=1 loose=1', sky_step(game))

    def test_name_match_failing_the_star_body_check(self, _running):
        """A nebula_*_stars_* name whose texels are not a star body (a lit nebula, 0 % below luma 16) is skipped
        not_star_body, catalogue member and loose file alike; the loose file is left byte for byte."""
        member, lit = 'dds/nebula_glow_stars_red_halo.pck', 'dds/nebula_lit_stars_halo_diff.dds'
        with tempfile.TemporaryDirectory() as folder:
            game = make_game(folder)
            write_catalogue(game / '04.cat', [(STARS, gzip.compress(star_texture(), mtime=0)),
                                              (member, gzip.compress(nebula_part(), mtime=0))])
            f = game / lit
            f.parent.mkdir(parents=True)
            f.write_bytes(nebula_part())
            self.assertEqual((st.classify(member), st.classify(lit)), (('glow_halo', 'red'), ('lit', 'halo')))
            code, text = run(BATCH + ['--sky', 'on', '--game', str(game), '--install'])
            self.assertEqual(code, 0, text)
            self.assertIn('1 star textures marked in 1 families', text)
            self.assertNotIn(member, cat_members(game / 'addon/02.cat'))
            out = sky_step(game)
            self.assertIn(f'skipped sky texture {lit}: not_star_body (loose file left alone)', out)
            self.assertIn('skipped not_star_body 2', out)
            self.assertEqual(f.read_bytes(), nebula_part())
            self.assertFalse(st.ledger_path(game).exists())

    def test_round_trip_through_the_proxy_parser(self, _running):
        """The in-place mark parses with lod_texture_mark.h's own parser (the runtime's)."""
        compiler = shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            self.skipTest('host C++ compiler required for the parser round trip')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            f = root / LOOSE
            f.parent.mkdir(parents=True)
            f.write_bytes(loose_stars())
            assets = unittest.mock.Mock(entries={'addon/dds/nebula_loosefam_stars_red_diff.dds': [
                dict(path=LOOSE, source='loose:' + LOOSE, loose=f)]})
            assets.candidates = lambda base: assets.entries.get('addon/' + base, [])
            res = st.mark_loose(root, assets)
            self.assertEqual([r['status'] for r in res['rows']], ['built'])
            m = res['rows'][0]['metrics']
            (root / 'check.cpp').write_text(Marks.CHECK)
            build = subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I',
                                    str(ROOT / 'src/proxy'), str(root / 'check.cpp'), '-o', str(root / 'check')],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            got = subprocess.run([str(root / 'check'), str(f)], capture_output=True, text=True, check=True).stdout
        self.assertEqual(tuple(int(v) for v in got.split()), (1, 1, m['slot'], m['lane'], m['k'], 2, 0))

    def test_refusals_do_not_fail_the_run(self, _running):
        """A loose file without a parsable DDS header, a read-only one and a loose .pck are reported and left
        unchanged; the catalogue star texture is still marked."""
        with tempfile.TemporaryDirectory() as folder:
            game = sky_game(folder)
            d = game / 'addon/dds'
            d.mkdir(parents=True)
            bad, ro, pck = (d / 'nebula_badfam_stars_white_diff.dds', d / 'nebula_rofam_stars_blue_diff.dds',
                            d / 'nebula_pckfam_stars_red_diff.pck')
            bad.write_bytes(b'not a dds')
            ro.write_bytes(loose_stars())
            pck.write_bytes(gzip.compress(loose_stars(), mtime=0))
            before = {p: p.read_bytes() for p in (bad, ro, pck)}
            ro.chmod(0o444)
            try:
                code, text = run(BATCH + ['--sky', 'on', '--game', str(game), '--install'])
                self.assertEqual(code, 0, text)
                out = sky_step(game)
                if os.access(ro, os.W_OK):
                    self.skipTest('running as a user who can write read-only files')
            finally:
                ro.chmod(0o644)
            self.assertIn('refused sky texture addon/dds/nebula_badfam_stars_white_diff.dds: loose_not_dds', out)
            self.assertIn('refused sky texture addon/dds/nebula_rofam_stars_blue_diff.dds: loose_read_only', out)
            self.assertIn('refused sky texture addon/dds/nebula_pckfam_stars_red_diff.pck: loose_pck', out)
            self.assertIn('1 star textures marked, 1 new + 0 unchanged', out)
            self.assertIn('marked in-place 0 loose; skipped loose_not_dds 1, loose_pck 1, loose_read_only 1', out)
            self.assertEqual({p: p.read_bytes() for p in (bad, ro, pck)}, before)
            self.assertFalse(st.ledger_path(game).exists())


if __name__ == '__main__':
    unittest.main()
