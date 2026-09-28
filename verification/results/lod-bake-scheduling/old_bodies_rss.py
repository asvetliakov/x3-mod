#!/usr/bin/env python3
"""Peak RSS of the bodies behind the 2026-09-23 "~7 GB per worker" figure, each baked in a fresh spawned
process as a batch worker does (original_assets + bake_attempt), against predicted_bake_bytes (reviewer's
script, adapted to the per-body launcher). Census rows as lod_overlay.py --batch --dry-run --mod none builds
them; nothing is written into the game.

  python3 old_bodies_rss.py SCRATCH_DIR [NAME ...]   (default: the three bodies below)
"""
import multiprocessing
import resource
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tools' / 'analysis'), str(ROOT / 'verification' / 'probe')]
import lod_overlay as L  # noqa: E402

BODIES = ('ships/Pirate/Pirate_M2', 'stations/station_scenes/tech/argon_tech_M_laser_cc', 'ships/owp/owp_large')


class Stop(Exception):
    pass


def one(args):
    game, opts, row = args
    assets = L.original_assets(Path(game))[0]
    init = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    res, lost = L.bake_attempt(assets, row, opts)
    scale = 1 if sys.platform == 'darwin' else 1024
    return (row['name'], init * scale, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * scale,
            L.predicted_bake_bytes(row), lost or (res or {}).get('refused'))


if __name__ == '__main__':
    scratch = Path(sys.argv[1])
    scratch.mkdir(parents=True, exist_ok=True)
    only = scratch / 'old_bodies.txt'
    only.write_text('\n'.join(sys.argv[2:] or BODIES) + '\n')
    got = {}

    def fake(game, markers, rows, opts, *a, **k):
        got.update(game=str(game), rows=rows, opts=opts)
        raise Stop()
    L.bake_rows = fake
    try:
        L.main(['--batch', '--dry-run', '--mod', 'none', '--jobs', '1', '--only', str(only), '--out', str(scratch)])
    except Stop:
        pass
    with multiprocessing.get_context('spawn').Pool(1, maxtasksperchild=1) as pool:
        for n, i, p, pr, ref in pool.imap(one, [(got['game'], got['opts'], r) for r in got['rows']]):
            print(n, 'init %.2f peak %.2f predicted %.2f GiB measured/predicted %.3f' % (i / 2**30, p / 2**30,
                                                                                       pr / 2**30, p / pr), ref or '')
