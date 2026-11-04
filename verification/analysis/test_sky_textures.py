"""Offline nebula sky bake (tools/analysis/sky_textures.py; docs/architecture/sector-background-modern.md, Decision B,
answers 3 and 4) on synthetic inputs: the recipe on a banded gradient against answer 4's limits, the opt-in detail's
hue lock, the X3ML mark round trip (payload 2 star textures, payload 3 nebula parts) through the proxy's own parser
(src/proxy/lod_texture_mark.h), eligibility, and the overlay batch's sky row group (lod_overlay.py --batch --sky):
members in the same slot, the marker's sky rows, reuse keyed on inputs_sha256 independently of the bodies, and the
candidate check counting the sky members as intended overrides. No game bytes."""
import contextlib
import gzip
import io
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

COLOUR = np.array([0.62, 0.86, 1.25], np.float32)    # a blue nebula tint (chroma > 8 over most of the ramp)


def smooth_luma(w, h, slope=0.15):
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    return 30 + slope * x + 18 * np.sin(2 * np.pi * y / h)


def banded_part(w=1024, h=512, band=4, zero_cols=64):
    """DXT1 DDS of a slow blue gradient quantised to `band`-level luma steps (plateaus 27 px wide, the DXT1 banding
    of the stock parts exaggerated) with an exact-black band on the left."""
    lum = smooth_luma(w, h)
    lum = np.floor(lum / band) * band + band / 2
    rgb = lum[..., None] * COLOUR
    rgb[:, :zero_cols] = 0
    img = np.concatenate([np.clip(rgb, 0, 255), np.full((h, w, 1), 255, np.float32)], -1).astype(np.uint8)
    return lod_atlas.dds_header(w, h, 1, 'DXT1') + lod_atlas.encode_dxt(img, 'DXT1')


def star_texture(n=64):
    """DXT1 DDS with 11 bright 2x2 stars (luma > 96) on black and a full mip chain."""
    img = np.zeros((n, n, 4), np.uint8)
    img[..., 3] = 255
    for i in range(11):
        y, x = 4 + (i * 13) % (n - 8), 4 + (i * 29) % (n - 8)
        img[y:y + 2, x:x + 2, :3] = (200, 210, 255)
    levels = lod_atlas.mip_chain(img.astype(np.float64))
    return lod_atlas.write_dds(levels, 'DXT1')


class Recipe(unittest.TestCase):
    def test_banded_gradient_against_answer_4(self):
        """The redefined answer-4 limits (2026-11-04): plateau share <= 10 % or <= 90 % of the source's, PSNR >= 40 dB,
        mean hue shift <= 2 deg, no zero violations, wrap columns kept; plus 2x size and the payload-3 mark. The
        identical-luma run is informational: the exact unbanded ramp, rounded to 8 bits, already has runs of several
        px (kept as the witness); the bake stays within that ideal and cuts the source's run by more than 3x."""
        src = banded_part()
        dds, info = st.bake_part(src, 'synthetic', 1, st.options_of())
        m = st.metrics(src, dds)
        th = st.THRESHOLDS
        self.assertEqual(st.failures(m), [])
        self.assertNotIn('run', th)
        self.assertEqual((info['width'], info['height']), (2048, 1024))
        self.assertGreater(m['plateau_share_source'], 0.5)                 # the input is banded
        self.assertLessEqual(m['plateau_share'], th['plateau_share'])       # this input meets the absolute branch
        self.assertGreaterEqual(m['psnr'], th['psnr'])
        self.assertLessEqual(m['hue_shift_deg'], th['hue_shift_deg'])
        self.assertEqual(m['zero_violations'], 0)
        self.assertGreater(m['zero_texels'], 0)
        self.assertLessEqual(abs(m['wrap_diff'] - m['wrap_diff_source']), th['wrap_tolerance'])
        ideal = np.clip(np.rint(smooth_luma(1024, 512)[..., None] * COLOUR), 0, 255).astype(np.uint8)
        ideal[:, :64] = 0
        ideal_run = st.mean_run_x(ideal)
        self.assertGreater(ideal_run, 1.5)                                  # witness: the dropped 1.5 px limit
        self.assertLessEqual(m['run'], ideal_run)
        self.assertLess(m['run'], m['run_source'] / 3)
        self.assertEqual(lod_atlas.read_dds_mark(dds), (1, st.fnv1a32(b'synthetic') & 255, 1, 0, st.PAYLOAD_SKY_NEBULA))
        self.assertEqual(lod_atlas.dds_format(dds), (2048, 1024, 1, 'DXT1'))
        self.assertEqual(struct.unpack_from('<I', dds, 28)[0], struct.unpack_from('<I', src, 28)[0])   # stock fields
        self.assertTrue((lod_atlas.decode_dds(dds)[..., 3] == 255).all())   # alpha 1 everywhere

    def test_detail_is_opt_in_and_hue_locked(self):
        src = banded_part()
        plain, _ = st.bake_part(src, 'synthetic', 1, st.options_of())
        detail, _ = st.bake_part(src, 'synthetic', 1, st.options_of(dict(detail=True)))
        self.assertNotEqual(plain, detail)
        m = st.metrics(src, detail)
        self.assertLess(m['hue_shift_deg'], 1.0)                            # the gain moves intensity, not hue
        self.assertEqual(m['zero_violations'], 0)
        self.assertEqual(st.options_of()['detail'], False)

    def test_zero_blocks_keep_black_and_alpha(self):
        rgb = np.full((8, 8, 3), 40.0, np.float32)
        zero = np.zeros((8, 8), bool)
        zero[:3, :3] = True                                                 # a mixed block and plain ones
        zero[4:, 4:] = True                                                 # an all-zero block
        rgb[zero] = 0
        level = st.encode_dxt1(rgb, zero)
        back = lod_atlas.decode_dds(lod_atlas.dds_header(8, 8, 1, 'DXT1') + level)
        self.assertTrue((back[zero][:, :3] == 0).all())
        self.assertTrue((back[..., 3] == 255).all())
        self.assertTrue((back[~zero][:, :3] > 0).any(-1).all())


def read_png(path):
    """RGB uint8 array of a filter-0 8-bit RGB PNG (what sky_textures.write_png writes)."""
    import zlib
    data = Path(path).read_bytes()
    w, h = struct.unpack('>II', data[16:24])
    pos, idat = 8, b''
    while pos < len(data):
        n, = struct.unpack('>I', data[pos:pos + 4])
        if data[pos + 4:pos + 8] == b'IDAT':
            idat += data[pos + 8:pos + 8 + n]
        pos += 12 + n
    rows = np.frombuffer(zlib.decompress(idat), np.uint8).reshape(h, w * 3 + 1)
    assert (rows[:, 0] == 0).all()
    return rows[:, 1:].reshape(h, w, 3)


class Preview(unittest.TestCase):
    def test_part_preview(self):
        """--sky-preview: one PNG per part (source 2x bilinear | 4 px grey | decoded bake), README, none for stars;
        --sky-preview-scale 2 halves both sides."""
        src = banded_part()
        w, h, _, _ = lod_atlas.dds_format(src)
        rows = [dict(member=PART, kind='part', family='testfam', number=1, data=src),
                dict(member=STARS, kind='stars', family='testfam', colour='blue', data=star_texture())]
        with tempfile.TemporaryDirectory() as folder:
            d = Path(folder) / 'pv'
            res = st.bake_rows(rows, st.options_of(), 1, keep_dds=True, preview_dir=d)
            self.assertEqual(sorted(p.name for p in d.iterdir()), ['README.txt', 'testfam_part_01.png'])
            img = read_png(d / 'testfam_part_01.png')
            self.assertEqual(img.shape, (2 * h, 2 * w * 2 + 4, 3))
            self.assertEqual(img[0, 0].tolist(), lod_atlas.decode_dds(src)[0, 0, :3].tolist())
            self.assertTrue((img[:, 2 * w:2 * w + 4] == st.PREVIEW_GREY).all())
            self.assertEqual(img[:, 2 * w + 4:].tolist(), lod_atlas.decode_dds(unpack(res[0]['stored']))[..., :3].tolist())
            self.assertIn('preview', res[0]['timings'])
            st.bake_rows(rows[:1], st.options_of(), 1, preview_dir=d, preview_scale=2)
            self.assertEqual(read_png(d / 'testfam_part_01.png').shape, (h, w * 2 + 4, 3))


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
        """Payload 2 and 3 through lod_texture_mark.h: the header constants and the parser's own decode."""
        header = (ROOT / 'src/proxy/lod_texture_mark.h').read_text()
        magic = int(re.search(r'magic = 0x([0-9a-f]+)u', header).group(1), 16)
        self.assertEqual(struct.pack('<I', magic), lod_atlas.DDS_MARK)
        self.assertEqual(int(re.search(r'mark_offset = (\d+)', header).group(1)), lod_atlas.DDS_MARK_OFFSET)
        self.assertEqual(int(re.search(r'version = (\d+);', header).group(1)), lod_atlas.DDS_MARK_VERSION)
        stars, sinfo = st.bake_stars(star_texture(), 'avdustysol', 'orange')
        part, _ = st.bake_part(banded_part(), 'bluewell', 3, st.options_of())
        want = [(1, 1, sinfo['slot'], 3, sinfo['k'], 2, 0), (1, 1, st.fnv1a32(b'bluewell') & 255, 3, 0, 3, 0)]
        for data, w in zip((stars, part), want):
            self.assertEqual(lod_atlas.read_dds_mark(data), w[1:6])
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
            (f / 'part.dds').write_bytes(part)
            out = subprocess.run([str(f / 'check'), str(f / 'stars.dds'), str(f / 'part.dds')], capture_output=True,
                                 text=True, check=True).stdout.split('\n')
            got = [tuple(int(v) for v in line.split()) for line in out if line]
        self.assertEqual(got, want)                     # lane_of 0: the strength lane is untouched by sky marks


class Eligibility(unittest.TestCase):
    def test_names_and_formats(self):
        self.assertEqual(st.classify('dds\\nebula_bluewell_background_part_02_diff.pck'), ('part', 'bluewell', 2))
        self.assertEqual(st.classify('addon2/dds/nebula_fogred_background_part1_diff.pck'), ('part', 'fogred', 1))
        self.assertEqual(st.classify('dds/nebula_fogred_background_diff.pck'), ('part', 'fogred', 0))
        self.assertEqual(st.classify('dds/nebula_red_reef_stars_orange_diff.pck'), ('stars', 'red_reef', 'orange'))
        self.assertIsNone(st.classify('dds/nebula_bluewell_dust_diff.pck'))
        self.assertIsNone(st.classify('objects/nebula_bluewell_background.pbb'))
        opts = st.options_of()
        dxt5 = lod_atlas.dds_header(1024, 512, 1, 'DXT5') + bytes(1024 * 512)
        self.assertEqual(st.eligibility('part', dxt5, opts)[:2], (False, 'alpha_format'))
        small = lod_atlas.dds_header(64, 64, 1, 'DXT1') + bytes(64 * 64 // 2)
        self.assertEqual(st.eligibility('part', small, opts)[:2], (False, 'small'))
        raw = lod_atlas.dds_header(512, 512, 1, 'A8R8G8B8') + bytes(512 * 512 * 4)
        self.assertEqual(st.eligibility('part', raw, opts)[:2], (False, 'uncompressed'))
        self.assertTrue(st.eligibility('part', raw, st.options_of(dict(uncompressed='dxt1')))[0])
        self.assertEqual(st.out_size(2048, 1024), (4096, 2048))
        self.assertEqual(st.out_size(2048, 2048), (2048, 2048))
        self.assertEqual(st.out_size(1024, 1024), (2048, 2048))
        self.assertEqual(st.out_size(4096, 2048), (4096, 2048))


PART = 'dds/nebula_testfam_background_part_01_diff.pck'
STARS = 'dds/nebula_testfam_stars_blue_diff.pck'


def sky_game(folder):
    game = make_game(folder)
    write_catalogue(game / '04.cat', [(PART, gzip.compress(banded_part(), mtime=0)),
                                      (STARS, gzip.compress(star_texture(), mtime=0))])
    return game


@unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
class OverlayGroup(unittest.TestCase):
    def test_members_marker_and_reuse(self, _running):
        """--sky on: the textures ride in the overlay slot under their stock names with a marker sky group; --sync
        reuses them by inputs_sha256; a sky recipe change rebakes the textures and no body; a body change rebakes
        that body and no texture; the candidate check counts the members as sky overrides, not collisions."""
        sky = BATCH + ['--sky', 'on']
        with tempfile.TemporaryDirectory() as folder:
            game = sky_game(folder)
            out1 = Path(folder) / 'out1'
            code, text = run(sky + ['--game', str(game), '--out', str(out1)])
            self.assertEqual(code, 0)
            self.assertIn('sky group (sky_textures.py', text)
            self.assertIn('2 textures (1 nebula parts, 1 star textures) in 1 families, built 2 + reused 0', text)
            marker = json.loads((out1 / 'addon/02.x3m-lod.json').read_text())
            g = marker['sky']
            self.assertEqual((g['recipe_id'], g['tool_sha256']), (st.RECIPE_ID, st.tool_sha256()))
            self.assertNotIn('sky_textures.py', lod_overlay.TOOL_FILES)
            both = (ROOT / 'tools/analysis/sky_textures.py').read_bytes() + (ROOT / 'tools/analysis/lod_atlas.py').read_bytes()
            self.assertEqual(g['tool_sha256'], lod_overlay.hashlib.sha256(both).hexdigest())   # lod_atlas rebakes textures
            self.assertEqual(sorted(r['member'] for r in g['rows']), [PART, STARS])
            members = cat_members(out1 / 'addon/02.cat')
            for r in g['rows']:
                self.assertEqual(lod_overlay.hashlib.sha256(members[r['member']]).hexdigest(), r['member_sha256'])
            part = unpack(members[PART])
            self.assertEqual(lod_atlas.dds_format(part)[:2], (2048, 1024))
            self.assertEqual(lod_atlas.read_dds_mark(part)[4], st.PAYLOAD_SKY_NEBULA)
            self.assertEqual(unpack(members[STARS])[44:], star_texture()[44:])
            record = json.loads((out1 / 'x3m-lod-batch.json').read_text())
            self.assertEqual(record['sky']['families']['testfam']['sizes'], ['2048x1024'])
            self.assertEqual(record['slots'][0]['sky'], 2)
            cand = lod_overlay_check.candidate_state(game, out1)
            self.assertEqual(cand['state'], 'ok', cand['clauses'])
            self.assertTrue(any('2 sky texture members override' in c for c in cand['clauses']))
            for name in ('02.cat', '02.dat', '02.x3m-lod.json'):
                shutil.copy(out1 / 'addon' / name, game / 'addon' / name)
            # --sync, nothing changed: every body and both textures reused
            out2 = Path(folder) / 'out2'
            code, text = run(sky + ['--sync', '--game', str(game), '--out', str(out2)])
            self.assertIn('built 0 + reused 9 = 9', text)
            self.assertIn('built 0 + reused 2', text)
            self.assertEqual(cat_members(out2 / 'addon/02.cat'), members)
            # a sky recipe change: the textures are rebaked, the bodies reused
            out3 = Path(folder) / 'out3'
            with unittest.mock.patch.object(st, 'RECIPE_ID', 'sky-test-recipe'):
                code, text = run(sky + ['--sync', '--game', str(game), '--out', str(out3)])
            self.assertIn('built 0 + reused 9 = 9', text)
            self.assertIn('built 2 + reused 0', text)
            # a body change: that body rebaked, the textures reused
            write_catalogue(game / '03.cat', [('objects/ships/x/good.pbb', unpack_free_body(game))])
            out4 = Path(folder) / 'out4'
            code, text = run(sky + ['--sync', '--game', str(game), '--out', str(out4)])
            self.assertIn('built 1 + reused 8 = 9', text)
            self.assertIn('built 0 + reused 2', text)
            m4 = json.loads((out4 / 'addon/02.x3m-lod.json').read_text())
            self.assertTrue(all(r.get('reused_from') == 2 for r in m4['sky']['rows']))
            # --sky off (AP auto): no group, no sky members
            out5 = Path(folder) / 'out5'
            code, text = run(BATCH + ['--game', str(game), '--out', str(out5)])
            self.assertIn('sky group off (--sky auto', text)
            self.assertNotIn('sky', json.loads((out5 / 'addon/02.x3m-lod.json').read_text()))
            self.assertNotIn(PART, cat_members(out5 / 'addon/02.cat'))

    def test_selected_package_sky_texture_goes_into_the_derived_copy(self, _running):
        """--mod NAME with --sky on: a star texture the package overrides is marked into the derived <mod>-x3m-lod copy
        (replacing the package's member once), the numbered overlay keeps the stock-view rows, and nothing raises."""
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
            self.assertEqual(sorted(r['member'] for r in numbered['sky']['rows']), [PART, STARS])

    def test_plateau_rule(self, _running):
        """Plateau branches: <= 0.10 or <= 0.9 x source below a source share of 0.5; at or above it (near-uniform)
        the bake must not raise the share and must cut the run to a third. An untouched bake and a nearest-neighbour
        2x of the banded input fail; avdustysol's measured figures (0.81 -> 0.73, run 19 -> 2.2) pass."""
        m = dict(plateau_share=0.43, plateau_share_source=0.48, psnr=50, hue_shift_deg=0.4, zero_violations=0,
                 wrap_diff=1.0, wrap_diff_source=1.0, run=2.0, run_source=2.4)
        self.assertEqual(st.failures(m), [])                                 # 0.43 <= 0.9 x 0.48
        self.assertTrue(st.failures(dict(m, plateau_share=0.44)))            # > max(0.10, 0.432)
        self.assertEqual(st.failures(dict(m, plateau_share=0.09, plateau_share_source=0.05)), [])
        self.assertTrue(st.failures(dict(m, hue_shift_deg=2.01)))
        avd = dict(m, plateau_share=0.73, plateau_share_source=0.81, run=2.2, run_source=19.0)
        self.assertEqual(st.failures(avd), [])
        self.assertTrue(st.failures(dict(avd, plateau_share=0.82)))          # near-uniform: share may not rise
        self.assertTrue(st.failures(dict(avd, run=6.4)))                     # near-uniform: run must drop to 1/3
        self.assertTrue(st.failures(dict(avd, psnr=39.9)))
        src = banded_part()
        self.assertTrue(st.failures(st.metrics(src, src)))                  # untouched bake
        w, h, _, _ = lod_atlas.dds_format(src)
        nn = np.repeat(np.repeat(lod_atlas.decode_dds(src), 2, 0), 2, 1)
        nn_dds = lod_atlas.dds_header(2 * w, 2 * h, 1, 'DXT1') + lod_atlas.encode_dxt(nn, 'DXT1')
        self.assertTrue(st.failures(st.metrics(src, nn_dds)))               # nearest-neighbour 2x

    def test_fl_layout_member_in_addon2(self, _running):
        """FL (addon2 slots, --sky auto = on): an eligible star texture lands in addon2/NN with its marker sky row."""
        from test_lod_overlay_batch import make_fl_game
        with tempfile.TemporaryDirectory() as folder:
            game, out = make_fl_game(folder), Path(folder) / 'out'
            (game / 'addon2/03.cat').unlink()
            (game / 'addon2/03.dat').unlink()
            write_catalogue(game / '03.cat', [(STARS, gzip.compress(star_texture(), mtime=0))])
            code, text = run(BATCH + ['--game', str(game), '--out', str(out), '--mod', 'none'])
            self.assertEqual(code, 0, text)
            marker = json.loads((out / 'addon2/02.x3m-lod.json').read_text())
            self.assertEqual(([r['member'] for r in marker['sky']['rows']], marker['edition']), ([STARS], 'fl'))
            self.assertEqual(lod_atlas.read_dds_mark(unpack(cat_members(out / 'addon2/02.cat')[STARS]))[4],
                             st.PAYLOAD_SKY_STARS)
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
            self.assertEqual(len(marker['sky']['rows']), 2)


def unpack_free_body(game):
    """A changed good.pbb (another threshold) for the body-change case, from test_lod_overlay_batch's helpers."""
    from test_lod_overlay_batch import atlas_tree_lod0, packed, with_threshold
    return packed(with_threshold(atlas_tree_lod0(), 6))


if __name__ == '__main__':
    unittest.main()
