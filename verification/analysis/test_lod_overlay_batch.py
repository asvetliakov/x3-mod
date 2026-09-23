"""Fleet batch mode of the merged-LOD overlay tool (tools/analysis/lod_overlay.py --batch) on synthetic
catalogues: .bob enumeration, text-body and ambiguous-extension refusals, trailing bytes, the
mixed-effects split, the second UV set with an occlusion decal, negative material indices,
qualified atlas names, tex/ jpg textures, the display-derived width, marker validation and
orphans, --sync reuse with slot retirement, and the addon/mods warning."""
import contextlib
import copy
import gzip
import io
import json
import shutil
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'analysis'))
import bob1
import lod_atlas
import lod_batch_census as census
import lod_overlay
import numpy as np
from inspect_x3 import read_catalogue
from sector_fog_census import unpack, write_catalogue
from test_bob1 import atlas_textures, atlas_tree_lod0


def packed(tree, trailing=b''):
    return gzip.compress(bob1.serialise(tree) + trailing, mtime=0)


def rec(i, m):
    return (i, 5000 + 1000 * m + i, 0, 65536, 0, 65536, 0)


def mixed_tree():
    """atlas_tree_lod0 with material 1 on another effect and a third record-0 group on material 0."""
    tree = atlas_tree_lod0()
    mats, r0 = bob1.materials(tree), bob1.lods(tree)[0]
    mats[1]['effect'] = b'other.fx'
    r0['parts'][0]['groups'].append({'material': 0, 'faces': [(1, 4, 5, 1)], 'extra': [rec(i, 0) for i in (1, 4, 5)]})
    return tree


def uv2_tree(decals=(b'x_decal.tga', b'x_decal.tga')):
    """atlas_tree_lod0 with a second UV pair on every record-0 point and an occlusion decal per material."""
    tree = atlas_tree_lod0()
    mats, r0 = bob1.materials(tree), bob1.lods(tree)[0]
    r0['points'] = [(p[0] | 4,) + p[1:6] + (p[4] // 2, p[5] // 2) + p[6:] for p in r0['points']]
    for m, d, s in zip(mats, decals, (65536, 32768)):
        m['params'] += [(b't_OcclusionTexture', 8, d), (b'g_MatOcclStr', 2, [s])]
    return tree


def with_threshold(tree, t):
    tree = copy.deepcopy(tree)
    bob1.lods(tree)[1]['value'] = t
    return tree


def jpg_texture():
    from PIL import Image
    y, x = np.mgrid[0:16, 0:16]
    img = np.stack([8 + 15 * x, 8 + 15 * y, np.full((16, 16), 100)], -1).astype(np.uint8)
    buf = io.BytesIO()
    Image.fromarray(img, 'RGB').save(buf, 'JPEG', quality=95)
    return buf.getvalue()


def jpg_tree():
    tree = atlas_tree_lod0()
    for m in bob1.materials(tree)[:2]:
        m['params'] = [(n, t, b'j_diff.tga' if n == b't_DiffuseTexture' else v) for n, t, v in m['params']]
    return tree


def neg_tree():
    tree = atlas_tree_lod0()
    bob1.lods(tree)[0]['parts'][0]['groups'][1]['material'] = -79
    return tree


def make_game(folder):
    game = Path(folder) / 'game'
    write_catalogue(game / '01.cat', atlas_textures() + [('tex/j_diff.jpg', jpg_texture())])
    body = bob1.serialise(atlas_tree_lod0())
    write_catalogue(game / '02.cat', [
        ('objects/ships/x/good.pbb', packed(atlas_tree_lod0())),
        ('objects/ships/x/bobby.bob', body),                                   # unpacked .bob member
        ('objects/ships/x/text.pbd', b'BODY 0\n'),                            # text body
        ('objects/ships/x/amb.pbb', packed(atlas_tree_lod0())),
        ('objects/ships/x/amb.pbd', b'BODY 0\n'),
        ('objects/ships/x/trail2.pbb', packed(atlas_tree_lod0(), b'OB')),     # 2 stray closer bytes
        ('objects/ships/x/trail9.pbb', packed(atlas_tree_lod0(), b'B' * 9)),
        ('objects/ships/x/mixed.pbb', packed(mixed_tree())),
        ('objects/ships/x/uv.pbb', packed(uv2_tree())),
        ('objects/ships/x/uvbad.pbb', packed(uv2_tree((b'x_decal.tga', b'NONE_OCCL_DECAL.dds')))),
        ('objects/ships/x/oob.pbb', packed(neg_tree())),
        ('objects/ships/x/jpg.pbb', packed(jpg_tree())),
        ('objects/stations/y/good.pbb', packed(with_threshold(atlas_tree_lod0(), 160))),   # stem twin, T_1 > T_pad
        ('objects/cut/00001.pbb', gzip.compress(b'CUT1' + b'\0' * 8, mtime=0))])
    write_catalogue(game / 'addon/01.cat', [('objects/ships/x/modship.bob', body)])   # a mod's catalogue
    return game


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = lod_overlay.main(argv)
    return code, out.getvalue()


def cat_members(cat):
    entries = {e['path']: e for e in read_catalogue(cat)}
    raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
    return {p: raw[e['offset']:e['offset'] + e['size']] for p, e in entries.items()}


BATCH = ['--batch', '--jobs', '1', '--atlas-size', '64', '--atlas-max-size', '128', '--min-texels', '0']


class Enumeration(unittest.TestCase):
    def test_census_rows(self):
        opts = dict(sizes=(64, 128), include_other=False, rule=dict(census.RULE), widths=(1280,))
        with tempfile.TemporaryDirectory() as folder:
            game = make_game(folder)
            rows, skipped = census.run(game, opts, include_text=True)
        by = {r['name']: r for r in rows}
        self.assertEqual(skipped, [])
        self.assertEqual(sorted(by), ['ships/x/amb', 'ships/x/bobby', 'ships/x/good', 'ships/x/jpg', 'ships/x/mixed',
                                      'ships/x/modship', 'ships/x/oob', 'ships/x/text', 'ships/x/trail2',
                                      'ships/x/trail9', 'ships/x/uv', 'ships/x/uvbad', 'stations/y/good'])
        self.assertTrue(by['ships/x/bobby']['eligible'] and by['ships/x/modship']['eligible'])    # .bob members
        self.assertEqual(by['ships/x/modship']['source'], 'addon/01.cat')
        self.assertEqual(by['ships/x/text']['refuse'], ['text_body'])
        self.assertEqual(by['ships/x/amb']['refuse'], ['ambiguous_body_ext'])
        self.assertEqual((by['ships/x/trail2']['trailing'], by['ships/x/trail2']['eligible']), (2, True))
        self.assertEqual(by['ships/x/trail9']['refuse'], ['trailing_bytes'])
        self.assertEqual((by['ships/x/mixed']['effects'], by['ships/x/mixed']['atlas_materials'],
                          by['ships/x/mixed']['r0_drawn'], by['ships/x/mixed']['c_drawn']), (2, 2, 3, 2))
        self.assertEqual((by['ships/x/uv']['uv2'], by['ships/x/uv']['occlusion']), (15, {'argon.fx': 'x_decal.tga'}))
        self.assertEqual(by['ships/x/uvbad']['refuse'], ['occlusion_mismatch'])
        self.assertEqual(by['ships/x/oob']['refuse'], ['material_outside_table'])
        self.assertEqual(by['ships/x/jpg']['texture_sources'][:2], ['dds/a_bump.pck', 'dds/b_light.pck'])
        self.assertIn('tex/j_diff.jpg', by['ships/x/jpg']['texture_sources'])
        self.assertTrue(by['stations/y/good']['eligible'] and by['stations/y/good']['t_pad_below_t1'])
        self.assertNotEqual(by['stations/y/good']['atlas_stem'], by['ships/x/good']['atlas_stem'])
        self.assertTrue(all(len(r['inputs_sha256']) == 64 for r in rows if r['eligible']))
        self.assertIn('trailing=2', census.format_row(by['ships/x/trail2']))

    def test_trailing_bytes_and_negative_index_in_plan_body(self):
        with tempfile.TemporaryDirectory() as folder:
            game = make_game(folder)
            assets, _ = lod_overlay.original_assets(game)
            p = lod_overlay.plan_body(assets, 'ships/x/trail2', 8, 'compact', collapse='two', source_record=0)
            self.assertEqual(p['trailing_bytes'], 2)
            self.assertNotIn('trailing_bytes', bob1.parse(unpack(p['stored'])))
            out = io.StringIO()
            lod_overlay.describe(p, out)
            self.assertIn('warning: 2 stray byte(s) after /BOB', out.getvalue())
            with self.assertRaisesRegex(SystemExit, 'more than the 8'):
                lod_overlay.plan_body(assets, 'ships/x/trail9', 8, 'compact', collapse='two')
            with self.assertRaisesRegex(SystemExit, r'group material index \[-79\] outside'):
                lod_overlay.plan_body(assets, 'ships/x/oob', 8, 'compact', collapse='two', source_record=0)
            with self.assertRaisesRegex(SystemExit, 'text body'):
                lod_overlay.plan_body(assets, 'ships/x/text', 8, 'compact', collapse='two')
            good = lod_overlay.plan_body(assets, 'stations/y/good', 8, 'compact', collapse='two', source_record=0)
            self.assertTrue(good['guard_waived'])                              # T_1 160 > T_pad 8, source record 0
            self.assertEqual([l['value'] for l in good['ladder']], [100, 160, 8])
            with self.assertRaisesRegex(SystemExit, 'must not be below'):        # guard kept for a coarser source
                lod_overlay.plan_body(assets, 'stations/y/good', 8, 'compact', collapse='two', source_record=1)

    def test_width_and_only_file(self):
        self.assertEqual((lod_overlay.effective_width(), lod_overlay.effective_width((2560, 1440)),
                          lod_overlay.effective_width((1920, 1080), 1280)), (1800, 2400, 1280))
        self.assertEqual(lod_overlay.parse_display('2560x1440'), (2560, 1440))
        with tempfile.TemporaryDirectory() as folder:
            p = Path(folder) / 'only.txt'
            p.write_text('== run255 (x frame 1): 3 bodies drawn\n    62 draws ships/argon/argon_TL T_pad=80\n'
                         '     2 draws objects/effects/hud (no binary body)\n  eligible 1; resident\n'
                         'stations/others/military_outpost_middleb=150@0\n# comment\nships/x/good\n')
            self.assertEqual(lod_overlay.parse_only(p), {'objects/ships/argon/argon_tl', 'objects/effects/hud',
                                                         'objects/stations/others/military_outpost_middleb',
                                                         'objects/ships/x/good'})


class BatchRun(unittest.TestCase):
    def test_dry_run_record(self):
        with tempfile.TemporaryDirectory() as folder:
            game, out = make_game(folder), Path(folder) / 'out'
            code, text = run(BATCH + ['--dry-run', '--game', str(game), '--out', str(out), '--jobs', '2'])
            self.assertEqual(code, 0)
            self.assertFalse((out / 'addon').exists())
            record = json.loads((out / 'x3m-lod-batch.json').read_text())
            by = {b['name']: b for b in record['bodies']}
            self.assertEqual(record['settings']['screen_width'], 1800)                # 1080 * 1280 / 768
            self.assertEqual((record['slot'], record['retired_slot'], record['counts']['enumerated']), (2, None, 13))
            self.assertEqual(record['counts']['overlay_bodies'], 8)
            self.assertEqual(record['refused'], {'ambiguous_body_ext': 1, 'material_outside_table': 1,
                                                 'occlusion_mismatch': 1, 'text_body': 1, 'trailing_bytes': 1})
            self.assertEqual((by['ships/x/good']['draws'], by['ships/x/mixed']['draws'], by['ships/x/uv']['draws']),
                             (1, 2, 1))                                              # drawn groups: atlas [+ one per effect]; hidden part excluded
            self.assertEqual(by['ships/x/mixed']['atlas_materials'], 2)
            self.assertEqual((by['ships/x/trail2']['trailing'], by['stations/y/good']['guard_waived']), (2, True))
            self.assertEqual(record['mixed_effect_bodies'], ['ships/x/mixed'])
            self.assertEqual(record['uv2_bodies'], ['ships/x/uv'])
            self.assertIn('tex/j_diff.jpg', by['ships/x/jpg']['texture_sources'])
            self.assertEqual(record['ratio']['measured'], 8)
            self.assertIn('addon/01.cat', by['ships/x/modship']['member'])
            self.assertTrue((out / 'x3m-lod-batch-summary.txt').exists())
            bodies = (out / 'x3m-lod-batch-bodies.txt').read_text()
            self.assertIn('compact guard waived', bodies)
            self.assertIn('non-dds sources diffuse=01.cat:tex/j_diff.jpg', bodies)
            self.assertIn('bodies enumerated 13', text)
            self.assertIn('dry run: nothing written', text)
            only = Path(folder) / 'only.txt'
            only.write_text('ships/x/good=80@0\nships/x/text\n')
            code, text = run(BATCH + ['--dry-run', '--game', str(game), '--out', str(out), '--only', str(only),
                                      '--record', str(Path(folder) / 'r.json'), '--display', '2560x1440'])
            record = json.loads((Path(folder) / 'r.json').read_text())
            self.assertEqual((record['counts']['enumerated'], record['counts']['overlay_bodies'],
                              record['settings']['screen_width']), (2, 1, 2400))
            self.assertIn('upper bound', text)

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_write_markers_sync_and_retire(self, _running):
        with tempfile.TemporaryDirectory() as folder:
            game = make_game(folder)
            out1 = Path(folder) / 'out1'
            code, text = run(BATCH + ['--game', str(game), '--out', str(out1)])
            self.assertEqual(code, 0)
            self.assertIn('target addon/02.cat', text)
            marker = json.loads((out1 / 'addon/02.x3m-lod.json').read_text())
            self.assertEqual(marker['overlay_sha256'], {k: v for (k, v) in zip(('cat', 'dat'), (
                lod_overlay.hash_files([out1 / 'addon/02.cat'])[str(out1 / 'addon/02.cat')],
                lod_overlay.hash_files([out1 / 'addon/02.dat'])[str(out1 / 'addon/02.dat')]))})
            self.assertEqual((marker['originals_mode'], marker['display'], marker['screen_width']),
                             ('fingerprint', [1920, 1080], 1800))
            self.assertEqual(marker['batch']['settings']['rule'], census.RULE)
            self.assertEqual((marker['collapse'], marker['batch']['settings']['tool_sha256']),
                             ('atlas', lod_overlay.tool_sha256()))
            bodies = {b['name']: b for b in marker['bodies']}
            members = cat_members(out1 / 'addon/02.cat')
            for b in bodies.values():                                   # every member recorded with its sha256
                for m in b['members']:
                    self.assertEqual(lod_overlay.hashlib.sha256(members[m['path']]).hexdigest(), m['sha256'])
            q_ship, q_station = (lod_overlay.qualified_stem(p) for p in ('objects/ships/x/good.pbb',
                                                                         'objects/stations/y/good.pbb'))
            self.assertNotEqual(q_ship, q_station)
            self.assertIn(f'dds/x3m_lod_{q_ship}_diffuse.pck', members)
            self.assertIn(f'dds/x3m_lod_{q_station}_diffuse.pck', members)
            self.assertEqual(sorted(p for p in members if p.startswith('objects/')),
                             ['objects/ships/x/bobby.bob', 'objects/ships/x/good.pbb', 'objects/ships/x/jpg.pbb',
                              'objects/ships/x/mixed.pbb', 'objects/ships/x/modship.bob', 'objects/ships/x/trail2.pbb',
                              'objects/ships/x/uv.pbb', 'objects/stations/y/good.pbb'])
            self.assertEqual(members['objects/ships/x/bobby.bob'][:4], b'BOB1')   # unpacked member stays unpacked
            # mixed effects: one atlas material per effect, one group each in part 0
            mixed = bob1.parse(unpack(members['objects/ships/x/mixed.pbb']))
            c, wm = bob1.lods(mixed)[1], bob1.materials(mixed)
            self.assertEqual([(g['material'], len(g['faces'])) for g in c['parts'][0]['groups']], [(3, 4), (4, 3)])
            self.assertEqual([m['effect'] for m in wm[3:]], [b'argon.fx', b'other.fx'])
            self.assertEqual(bodies['ships/x/mixed']['atlas']['materials'], [3, 4])
            # second UV set passed through; merged material keeps the decal and the g_MatOcclStr mean
            uv = bob1.parse(unpack(members['objects/ships/x/uv.pbb']))
            c, wm = bob1.lods(uv)[1], bob1.materials(uv)
            src = bob1.lods(uv2_tree())[0]
            self.assertTrue(all(p[0] & 4 for p in c['points']))
            self.assertEqual({p[6:8] for p in c['points']}, {p[6:8] for p in src['points']})
            params = {n: v for n, t, v in wm[3]['params']}
            self.assertEqual((params[b't_OcclusionTexture'], params[b'g_MatOcclStr']), (b'x_decal.tga', [50371]))   # area-weighted mean of 65536 and 32768
            self.assertEqual(bodies['ships/x/uv']['atlas']['uv2_points'], 15)
            # jpg texture decoded through Pillow into the diffuse atlas
            jpg = bodies['ships/x/jpg']['atlas']
            self.assertEqual(jpg['tiles'][0]['sources']['diffuse'], '01.cat:tex/j_diff.jpg')
            dds = unpack(members[f'dds/x3m_lod_{lod_overlay.qualified_stem("objects/ships/x/jpg.pbb")}_diffuse.pck'])
            self.assertLess(jpg['check']['slots']['diffuse']['box_rgb'][0], 6)

            # install the overlay by hand: the marker validates against the files beside it
            for name in ('02.cat', '02.dat', '02.x3m-lod.json'):
                shutil.copy(out1 / 'addon' / name, game / 'addon' / name)
            (m,) = lod_overlay.installed_markers(game)
            self.assertEqual((m['slot'], m['status']), (2, 'valid'))
            assets, skipped = lod_overlay.original_assets(game)
            self.assertEqual(skipped, ['addon/02.cat'])
            # --sync: nothing changed, every body reused from the previous overlay dat
            out2 = Path(folder) / 'out2'
            code, text = run(BATCH + ['--sync', '--game', str(game), '--out', str(out2)])
            self.assertIn('built 0 + reused 8 = 8', text)
            self.assertEqual(cat_members(out2 / 'addon/02.cat'), members)
            m2 = json.loads((out2 / 'addon/02.x3m-lod.json').read_text())
            self.assertTrue(all(b.get('reused_from') == 2 for b in m2['bodies']))
            # a changed source member is rebuilt, the rest reused
            write_catalogue(game / '03.cat', [('objects/ships/x/good.pbb', packed(with_threshold(atlas_tree_lod0(), 6)))])
            out3 = Path(folder) / 'out3'
            code, text = run(BATCH + ['--sync', '--game', str(game), '--out', str(out3)])
            self.assertIn('built 1 + reused 7 = 8', text)
            m3 = json.loads((out3 / 'addon/02.x3m-lod.json').read_text())
            rebuilt = [b['name'] for b in m3['bodies'] if 'reused_from' not in b]
            self.assertEqual(rebuilt, ['ships/x/good'])
            self.assertEqual(m3['bodies'][[b['name'] for b in m3['bodies']].index('ships/x/good')]['source'], '03.cat')
            # a mod added a higher addon number: the new overlay takes 04, 02 is retired to an empty catalogue
            write_catalogue(game / 'addon/03.cat', [('objects/ships/x/modship.bob', bob1.serialise(atlas_tree_lod0()))])
            out4 = Path(folder) / 'out4'
            code, text = run(BATCH + ['--sync', '--game', str(game), '--out', str(out4)])
            self.assertIn('target addon/04.cat', text)
            self.assertIn('addon/02 retired', text)
            self.assertEqual(sorted(p.name for p in (out4 / 'addon').iterdir()),
                             ['02.cat', '02.dat', '02.x3m-lod.json', '04.cat', '04.dat', '04.x3m-lod.json'])
            self.assertEqual([e['path'] for e in read_catalogue(out4 / 'addon/02.cat')], ['x3m_lod/retired_02.txt'])
            self.assertGreater((out4 / 'addon/02.dat').stat().st_size, 0)             # never a 0-byte DAT
            retired = json.loads((out4 / 'addon/02.x3m-lod.json').read_text())
            self.assertEqual((retired['retired'], retired['retired_by'], retired['slot']), (True, 4, 2))
            m4 = json.loads((out4 / 'addon/04.x3m-lod.json').read_text())
            self.assertEqual((m4['slot'], m4['batch']['retired_slot']), (4, 2))
            self.assertEqual(sum(1 for b in m4['bodies'] if 'reused_from' in b), 7)   # modship: same bytes in addon/03
            # --install with the retire path: 02 becomes the empty pair, 04 the overlay; markers validate
            (game / 'addon/03.cat').unlink(), (game / 'addon/03.dat').unlink()
            write_catalogue(game / 'addon/03.cat', [('objects/ships/x/modship.bob', bob1.serialise(atlas_tree_lod0()))])
            code, text = run(BATCH + ['--sync', '--game', str(game), '--install'])
            self.assertIn('retired addon/02', text)
            statuses = {m['slot']: m['status'] for m in lod_overlay.installed_markers(game)}
            self.assertEqual(statuses, {2: 'retired', 4: 'valid'})
            self.assertEqual([e['path'] for e in read_catalogue(game / 'addon/02.cat')], ['x3m_lod/retired_02.txt'])
            self.assertEqual(lod_overlay.next_slot(game), 5)
            self.assertEqual(sorted((game / 'addon').glob('*.x3m-replaced')), [])
            # orphan: a mod overwrote our slot 04; the marker is reported, the catalogue read as a source,
            # and --install removes the marker
            write_catalogue(game / 'addon/04.cat', [('objects/ships/x/modship.bob', bob1.serialise(mixed_tree()))])
            statuses = {m['slot']: m['status'] for m in lod_overlay.installed_markers(game)}
            self.assertEqual(statuses, {2: 'retired', 4: 'orphaned'})
            assets, skipped = lod_overlay.original_assets(game)
            self.assertEqual(skipped, ['addon/02.cat'])         # retired 02 (one inert member) skipped; 04 (orphaned) is a source
            self.assertEqual(bob1.resolve_body(assets, 'ships/x/modship')['source'], 'addon/04.cat')
            code, text = run(BATCH + ['--game', str(game), '--install'])
            self.assertIn('orphaned marker 04.x3m-lod.json', text)
            self.assertIn('target addon/05.cat', text)
            self.assertIn('removed orphaned marker 04.x3m-lod.json', text)
            self.assertEqual(sorted(p.name for p in (game / 'addon').glob('*.x3m-lod.json')),
                             ['02.x3m-lod.json', '05.x3m-lod.json'])
            m5 = json.loads((game / 'addon/05.x3m-lod.json').read_text())
            self.assertEqual({b['name']: b['source'] for b in m5['bodies']}['ships/x/modship'], 'addon/04.cat')
            self.assertFalse((game / 'addon/06.cat').exists())

    def test_texel_floor_and_jobs_default(self):
        import os
        ram = lod_overlay.host_memory_bytes()
        want = min((os.cpu_count() or 2) - 2, 6)
        if ram:
            want = min(want, max(1, ram // (7 << 30) - 1))
        self.assertEqual(lod_overlay.build_parser().parse_args(['--batch']).jobs, max(1, want))
        with unittest.mock.patch.object(lod_overlay, 'host_memory_bytes', return_value=24 << 30):
            self.assertEqual(lod_overlay.default_jobs(), max(1, min((os.cpu_count() or 2) - 2, 2)))
        with unittest.mock.patch.object(lod_overlay, 'host_memory_bytes', return_value=None):
            self.assertEqual(lod_overlay.default_jobs(), max(1, min((os.cpu_count() or 2) - 2, 6)))
        self.assertEqual(lod_overlay.build_parser().parse_args(['--batch']).min_texels, 0.5)
        with tempfile.TemporaryDirectory() as folder:
            game, out = make_game(folder), Path(folder) / 'out'
            argv = [a for a in BATCH if a != '0'][:-1]                       # default --min-texels
            code, text = run(argv + ['--dry-run', '--game', str(game), '--out', str(out), '--min-texels', '100'])
            record = json.loads((out / 'x3m-lod-batch.json').read_text())
            self.assertEqual((record['counts']['overlay_bodies'], record['refused']['texel_floor']), (0, 8))
            floor = record['ratio']['texel_floor']
            self.assertEqual(len(floor), 8)
            self.assertTrue(all(0 < v < 100 for v in floor.values()))
            by = {b['name']: b for b in record['bodies']}
            self.assertEqual(by['ships/x/good']['refuse'], ['texel_floor'])
            self.assertAlmostEqual(by['ships/x/good']['ratio'], floor['ships/x/good'])
            self.assertIn(f'refused texel_floor (< 100) 8: ', text)
            self.assertIn(f'ships/x/good {floor["ships/x/good"]:.2f}', text)
            self.assertIn('dry run: nothing written', text)
            with self.assertRaisesRegex(SystemExit, 'texel_floor'):        # single-body atlas path, same option
                run(['--game', str(game), '--dry-run', '--collapse', 'atlas', '--atlas-size', '64',
                     '--min-texels', '100', 'ships/x/good=8@0'])
            self.assertIn('target addon/02.cat', run(['--game', str(game), '--dry-run', '--collapse', 'atlas',
                                                      '--atlas-size', '64', '--min-texels', '0', 'ships/x/good=8@0'])[1])

    @unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[])
    def test_legacy_marker_orphaned_unless_verified(self, _running):
        """A mod overwrote the slot of a legacy (pre-hash) marker: the catalogue is a source, nothing of it is
        retired or replaced; a legacy marker whose bodies verify stays ours (the pilot's shape)."""
        with tempfile.TemporaryDirectory() as folder:
            game = make_game(folder)
            (game / 'addon/02.x3m-lod.json').write_text(json.dumps(dict(slot=2, bodies=[], originals_sha256='x')))
            write_catalogue(game / 'addon/02.cat', [('objects/ships/x/modonly.pbb', packed(mixed_tree()))])
            write_catalogue(game / 'addon/03.cat', [('objects/ships/x/modship.bob', bob1.serialise(mixed_tree()))])
            mod02 = (game / 'addon/02.dat').read_bytes()
            self.assertEqual([(m['slot'], m['status']) for m in lod_overlay.installed_markers(game)], [(2, 'orphaned')])
            self.assertEqual(lod_overlay.original_assets(game)[1], [])
            code, text = run(BATCH + ['--game', str(game), '--install'])
            self.assertIn('target addon/04.cat', text)
            self.assertNotIn('retired', text.split('record ')[0].replace('orphaned marker', ''))
            self.assertEqual((game / 'addon/02.dat').read_bytes(), mod02)                  # the mod's files untouched
            self.assertEqual([e['path'] for e in read_catalogue(game / 'addon/02.cat')], ['objects/ships/x/modonly.pbb'])
            self.assertEqual(sorted(p.name for p in (game / 'addon').glob('*.x3m-lod.json')), ['04.x3m-lod.json'])
            m4 = json.loads((game / 'addon/04.x3m-lod.json').read_text())
            self.assertIn('ships/x/modonly', {b['name'] for b in m4['bodies']})
            # a legacy marker whose named bodies verify by decoded sha256 is ours
            legacy = dict(m4, bodies=[dict(name=b['name'], member=b['member'],
                                           overlay_decoded_sha256=b['overlay_decoded_sha256']) for b in m4['bodies']])
            legacy.pop('overlay_sha256')
            (game / 'addon/04.x3m-lod.json').write_text(json.dumps(legacy))
            self.assertEqual({m['slot']: m['status'] for m in lod_overlay.installed_markers(game)}, {4: 'legacy'})
            self.assertEqual(lod_overlay.original_assets(game)[1], ['addon/04.cat'])
            legacy['bodies'][0]['overlay_decoded_sha256'] = '0' * 64                       # a member that differs
            (game / 'addon/04.x3m-lod.json').write_text(json.dumps(legacy))
            self.assertEqual({m['slot']: m['status'] for m in lod_overlay.installed_markers(game)}, {4: 'orphaned'})

    def test_build_gate_and_specular_slot(self):
        with tempfile.TemporaryDirectory() as folder:
            game = make_game(folder)
            assets, _ = lod_overlay.original_assets(game)
            tree = bob1.parse(bob1.serialise(atlas_tree_lod0()))
            mats, r0 = bob1.materials(tree), bob1.lods(tree)[0]
            res = lod_atlas.build(assets, 'b', list(mats), r0, set(), 8, (64, 128), specular=True)
            self.assertEqual(res['slots'], ('diffuse', 'light', 'bump'))                  # no t_SpecularTexture
            spec = [dict(m, params=m['params'] + [(b't_SpecularTexture', 8, b'a_diff.tga')]) if i == 0 else m
                    for i, m in enumerate(mats)]
            res = lod_atlas.build(assets, 'b', list(spec), r0, set(), 8, (64, 128), specular=True)
            self.assertEqual(res['slots'], ('diffuse', 'light', 'bump', 'specular'))
            good = dict(res['check'])
            with unittest.mock.patch.object(lod_atlas, 'check', return_value=dict(good, inside=good['vertices'] - 1)), \
                    self.assertRaisesRegex(lod_atlas.AtlasError, 'outside their tile content'):
                lod_atlas.build(assets, 'b', list(mats), r0, set(), 8, (64, 128))
            with unittest.mock.patch.object(lod_atlas, 'check', return_value=dict(good, max_map_error_texels=1.5)), \
                    self.assertRaisesRegex(lod_atlas.AtlasError, 'inverse-map error'):
                lod_atlas.build(assets, 'b', list(mats), r0, set(), 8, (64, 128))
            bad = dict(res)
            bad['info'] = dict(res['info'], faces=[(pi, sf, of, 99, ti) for pi, sf, of, mi, ti in res['info']['faces']])
            with self.assertRaisesRegex(lod_atlas.AtlasError, 'placed in tile'):
                lod_atlas.check(r0, res['record'], bad, {}, spec + [dict(spec[0])] * 100)

    def test_mods_warning_and_single_mode_markers(self):
        with tempfile.TemporaryDirectory() as folder:
            game, out = make_game(folder), Path(folder) / 'out'
            write_catalogue(game / 'addon/mods/Big.cat', [('objects/ships/x/good.pbb', packed(atlas_tree_lod0())),
                                                          ('objects/ships/x/nothere.pbb', packed(atlas_tree_lod0()))])
            code, text = run(BATCH + ['--dry-run', '--game', str(game), '--out', str(out)])
            self.assertIn('warning: addon/mods/Big.cat (2 bodies) overrides the overlay for 1 of its 8 bodies', text)
            record = json.loads((out / 'x3m-lod-batch.json').read_text())
            self.assertTrue(any('Big.cat' in n for n in record['notes']))
            # single-body mode: an orphaned marker does not block, a live one does
            (game / 'addon/01.x3m-lod.json').write_text(json.dumps(dict(slot=1, overlay_sha256={'cat': 'x', 'dat': 'y'})))
            code, text = run(['--game', str(game), '--dry-run', '--collapse', 'two', 'ships/x/good=8@0'])
            self.assertIn('warning: marker 01.x3m-lod.json is orphaned', text)
            self.assertIn('target addon/02.cat', text)
            (game / 'addon/01.x3m-lod.json').write_text(json.dumps(dict(slot=1, bodies=[])))   # legacy, no proof
            self.assertEqual([m['status'] for m in lod_overlay.installed_markers(game)], ['orphaned'])
            self.assertIn('target addon/02.cat', run(['--game', str(game), '--dry-run', '--collapse', 'two',
                                                      'ships/x/good=8@0'])[1])
            h = lod_overlay.hash_files([game / 'addon/01.cat', game / 'addon/01.dat'])   # valid hashes: live
            (game / 'addon/01.x3m-lod.json').write_text(json.dumps(dict(
                slot=1, bodies=[], overlay_sha256={'cat': h[str(game / 'addon/01.cat')], 'dat': h[str(game / 'addon/01.dat')]})))
            with self.assertRaisesRegex(SystemExit, 'already installed'):
                run(['--game', str(game), '--dry-run', '--collapse', 'two', 'ships/x/good=8@0'])


if __name__ == '__main__':
    unittest.main()
