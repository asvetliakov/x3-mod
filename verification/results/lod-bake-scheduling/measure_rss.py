#!/usr/bin/env python3
"""Peak RSS of one LOD overlay bake worker per body, against predictors known before the bake.

  capture: the batch census as lod_overlay.py --batch --dry-run --mod none runs it (bake_rows intercepted, nothing
           baked or written into the game), rows to bake pickled under SCRATCH (--jobs for the census)
  measure: the sample (ten slowest, ten largest members, ten most textures by the installed x3m-lod-batch.json,
           ten largest texture_pixels, plus about forty spread by seconds) baked one body per fresh spawned
           process, as a batch worker does (original_assets + bake_attempt); peak = ru_maxrss of that process
  fit:     peak ~ BASE + K * texture_pixels (lod_overlay.predicted_bake_bytes) against the sample: worst
           under-prediction before and after the safety factor; prints the compact table to stdout

  PYTHONPATH=tools/analysis:verification/probe python3 verification/results/lod-bake-scheduling/measure_rss.py
      capture|measure|fit --scratch DIR [--jobs N] [--parallel P]
Reads the game directory only (bob1.DEFAULT_GAME); writes under --scratch and sample.json beside this script.
"""
import argparse
import json
import os
import pickle
import resource
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path[:0] = [str(ROOT / 'tools' / 'analysis'), str(ROOT / 'verification' / 'probe')]
import lod_overlay  # noqa: E402

FEATURES = ('texture_pixels', 'r0_faces', 'r0_points', 'r0_bytes', 'tiles', 'atlas_materials')


def features(row, width):
    est = (row.get('atlas') or {}).get(width, {})
    f = {k: row.get(k) for k in FEATURES}
    f.update(name=row['name'], atlas_size=est.get('size'), slots=len(row.get('slots') or ()),
             textures=len(row.get('texture_sources') or ()))
    return f


class Captured(Exception):
    pass


def capture(a):
    got = {}

    def fake(game, markers, rows, atlas_opts, *rest, **kw):      # the batch's bake_rows arguments
        if not got:
            got.update(game=str(game), rows=rows, atlas_opts=atlas_opts)
            raise Captured()
        return []
    lod_overlay.bake_rows = fake
    t0 = time.time()
    try:
        lod_overlay.main(['--batch', '--dry-run', '--mod', 'none', '--jobs', str(a.jobs),
                          '--record', str(Path(a.scratch) / 'unused-record.json')])
    except Captured:
        pass
    got['census_s'] = round(time.time() - t0, 1)
    with open(Path(a.scratch) / 'rows.pkl', 'wb') as f:
        pickle.dump(got, f)
    width = got['atlas_opts']['screen_width']
    with open(Path(a.scratch) / 'features.json', 'w') as f:
        json.dump([features(r, width) for r in got['rows']], f)
    print(f"captured {len(got['rows'])} rows to bake; census {got['census_s']} s with {a.jobs} jobs")


def measure_one(args):
    game, atlas_opts, row = args
    assets = lod_overlay.original_assets(Path(game))[0]
    init = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    res, lost = lod_overlay.bake_attempt(assets, row, atlas_opts)
    res = res or dict(refused=lost)
    peak = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    scale = 1 if sys.platform == 'darwin' else 1024      # ru_maxrss: bytes on macOS, KiB on Linux
    return dict(name=row['name'], init_bytes=init * scale, peak_bytes=peak * scale,
                seconds=round(res.get('seconds', 0.0), 2), refused=res.get('refused', '')[:120] or None)


def sample(names_rows, installed):
    by = {r['name']: r for r in names_rows}
    inst = {b['name']: b for b in installed if b['name'] in by and b.get('seconds') is not None}
    pick = []

    def top(key, n=10):
        for name in sorted(by, key=key, reverse=True)[:n]:
            if name not in pick:
                pick.append(name)
    top(lambda n: (inst.get(n) or {}).get('seconds', 0))
    top(lambda n: (inst.get(n) or {}).get('member_bytes', 0))
    top(lambda n: len((inst.get(n) or {}).get('texture_sources') or ()))
    top(lambda n: by[n].get('texture_pixels') or 0)
    rest = sorted((n for n in by if n not in pick), key=lambda n: (inst.get(n) or {}).get('seconds', 0))
    step = max(1, len(rest) // 40)
    pick += rest[step // 2::step][:40]
    return pick


def measure(a):
    import multiprocessing
    with open(Path(a.scratch) / 'rows.pkl', 'rb') as f:
        got = pickle.load(f)
    installed = json.load(open(Path(got['game']) / 'addon' / 'x3m-lod-batch.json'))['bodies']
    width = got['atlas_opts']['screen_width']
    feats = [features(r, width) for r in got['rows']]
    names = sample(feats, installed)
    by = {r['name']: r for r in got['rows']}
    work = [(got['game'], got['atlas_opts'], by[n]) for n in names]
    t0 = time.time()
    out = []
    with multiprocessing.get_context('spawn').Pool(a.parallel, maxtasksperchild=1) as pool:
        for i, res in enumerate(pool.imap_unordered(measure_one, work, chunksize=1)):
            out.append(res)
            print(f"{i + 1}/{len(work)} {res['name']} peak {res['peak_bytes'] / 2**30:.2f} GiB {res['seconds']} s",
                  flush=True)
    inst = {b['name']: b for b in installed}
    fb = {f['name']: f for f in feats}
    for r in out:
        r.update({k: v for k, v in fb[r['name']].items() if k != 'name'})
        r['installed_seconds'] = (inst.get(r['name']) or {}).get('seconds')
    out.sort(key=lambda r: -r['peak_bytes'])
    json.dump(dict(parallel=a.parallel, wall_s=round(time.time() - t0, 1), census_s=got.get('census_s'),
                   rows_to_bake=len(got['rows']), sample=out), open(HERE / 'sample.json', 'w'), indent=0)
    print(f'{len(out)} bodies measured in {time.time() - t0:.0f} s -> {HERE / "sample.json"}')


def fit(a):
    d = json.load(open(HERE / 'sample.json'))
    s = d['sample']
    safety = lod_overlay.BAKE_SAFETY
    raw = lambda r: lod_overlay.predicted_bake_bytes(r) / safety
    print(f'model: raw = {lod_overlay.BAKE_BASE_BYTES >> 20} MiB + {lod_overlay.BAKE_BYTES_PER_PIXEL} B x texture_pixels'
          f' + {lod_overlay.BAKE_BYTES_PER_ATLAS_TEXEL} B x atlas side^2 x slots + {lod_overlay.BAKE_BYTES_PER_FACE} B x'
          f' r0_faces; predicted = raw x {safety}')
    peaks = sorted(r['peak_bytes'] for r in s)
    print(f"sample {len(s)} bodies: peak max {peaks[-1] / 2**30:.2f} GiB, median {peaks[len(peaks) // 2] / 2**30:.2f},"
          f" min {peaks[0] / 2**30:.2f}; worker before its body (init) max {max(r['init_bytes'] for r in s) / 2**30:.2f} GiB")
    w = max(s, key=lambda r: r['peak_bytes'] / raw(r))
    print(f"worst raw under-prediction: {w['name']} measured {w['peak_bytes'] / 2**30:.2f} GiB, raw"
          f" {raw(w) / 2**30:.2f} GiB (measured/raw {w['peak_bytes'] / raw(w):.3f})")
    w = max(s, key=lambda r: r['peak_bytes'] / lod_overlay.predicted_bake_bytes(r))
    print(f"worst after safety: {w['name']} measured/predicted {w['peak_bytes'] / lod_overlay.predicted_bake_bytes(w):.3f}")
    print(f"sum predicted / sum measured: {sum(map(lod_overlay.predicted_bake_bytes, s)) / sum(peaks):.2f}")
    feats = json.load(open(Path(a.scratch) / 'features.json'))
    preds = sorted(lod_overlay.predicted_bake_bytes(f) for f in feats)
    print(f"all {len(feats)} rows to bake: predicted max {preds[-1] / 2**30:.2f} GiB, p90"
          f" {preds[int(.9 * len(preds))] / 2**30:.2f}, median {preds[len(preds) // 2] / 2**30:.2f} GiB")
    print('name | peak GiB | texture Mpx | atlas | faces | bake s | predicted GiB')
    for r in s:
        print(f"{r['name']} | {r['peak_bytes'] / 2**30:.2f} | {r['texture_pixels'] / 1e6:.1f} | {r['atlas_size']} |"
              f" {r['r0_faces']} | {r['seconds']} | {lod_overlay.predicted_bake_bytes(r) / 2**30:.2f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('step', choices=('capture', 'measure', 'fit'))
    ap.add_argument('--scratch', required=True)
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--parallel', type=int, default=2)
    a = ap.parse_args()
    os.makedirs(a.scratch, exist_ok=True)
    dict(capture=capture, measure=measure, fit=fit)[a.step](a)


if __name__ == '__main__':
    main()
