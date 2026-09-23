"""A mod overwrites the slot of a legacy (pre-hash) x3m-lod marker, then batch --install runs (synthetic root)."""
import sys, json, tempfile, io, contextlib, unittest.mock, hashlib
from pathlib import Path
R = Path(__file__).resolve().parents[4]
sys.path[:0] = [str(R / 'verification/analysis'), str(R / 'tools/analysis'), str(R / 'verification/probe')]
import bob1, lod_overlay
from sector_fog_census import write_catalogue
from inspect_x3 import read_catalogue
from test_lod_overlay_batch import make_game, BATCH, packed, mixed_tree
with tempfile.TemporaryDirectory() as d:
    game = make_game(d)
    # legacy marker for addon/02 (the pilot's marker shape: no overlay_sha256); the mod then ships its own 02 and 03
    (game / 'addon/02.x3m-lod.json').write_text(json.dumps(dict(slot=2, bodies=[], originals_sha256='x')))
    write_catalogue(game / 'addon/02.cat', [('objects/ships/x/modonly.pbb', packed(mixed_tree()))])
    write_catalogue(game / 'addon/03.cat', [('objects/ships/x/modship.bob', bob1.serialise(mixed_tree()))])
    mod02 = hashlib.sha256((game / 'addon/02.dat').read_bytes()).hexdigest()
    print('markers', [(m['slot'], m['status']) for m in lod_overlay.installed_markers(game)])
    print('skipped as sources', lod_overlay.original_assets(game)[1])
    out = io.StringIO()
    with unittest.mock.patch.object(lod_overlay, 'running_game', return_value=[]), contextlib.redirect_stdout(out):
        lod_overlay.main(BATCH + ['--game', str(game), '--install'])
    print([l for l in out.getvalue().splitlines() if 'target' in l or 'retired' in l or l.startswith('wrote')][:3])
    print('addon/02 entries after', read_catalogue(game / 'addon/02.cat'), 'dat bytes', (game / 'addon/02.dat').stat().st_size,
          'mod 02.dat preserved anywhere', any(hashlib.sha256(p.read_bytes()).hexdigest() == mod02 for p in (game / 'addon').iterdir() if p.is_file()))
