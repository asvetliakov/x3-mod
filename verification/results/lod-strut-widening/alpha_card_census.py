#!/usr/bin/env python3
"""Fleet census of alpha-tested card materials in the overlay bodies' record 0 (the source of C): per body the
materials with g_ALPHATESTENABLE 1, their state (blend, z-write), their area share of the record, the share of
that area on sub-pixel patches at s_d = T_pad / 2 (5120x1440, F 1280; thin_geometry_census.face_geometry), and
whether the diffuse texture's alpha has holes at mip 0 (share of texels under 128 between 2 % and 98 %: a
lattice / cutout card) or is solid. Host-side read of the bottle catalogues through the atlas resolver.
Usage: python3 alpha_card_census.py [--jobs N] [--out FILE]"""
import argparse, json, sys, time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
import numpy as np
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[2] / 'tools' / 'analysis'))
import bob1, lod_atlas, body_materials, sector_fog_census as sfc  # noqa: E402
from thin_geometry_census import GAME, FOCAL, W_PX, manifest_bodies, face_geometry  # noqa: E402


def flag(m, key):
    for n, t, v in m.get('params', ()):
        if (n.decode('latin1') if isinstance(n, bytes) else n).lower() == key.lower() and t != 8:
            return v[0] if v else None
    return None


def one(args):
    name, t_pad = args
    assets = sfc.Assets(GAME)
    try:
        tree = bob1.parse(assets.read_entry(bob1.resolve_body(assets, name)), 8)
    except Exception as exc:  # noqa: BLE001
        return dict(name=name, error=str(exc))
    mats = bob1.materials(tree); rec = bob1.lods(tree)[0]
    geo = face_geometry(rec)
    if geo is None:
        return dict(name=name, error='no faces')
    tested = {i for i, m in enumerate(mats) if 'params' in m and flag(m, 'g_ALPHATESTENABLE') == 1}
    if not tested:
        return dict(name=name, t_pad=t_pad, faces=int(len(geo['F'])), tested=[], tested_area=0.0, thin_tested_area=0.0)
    k = FOCAL['5120x1440'] * (t_pad / 2) / (geo['r_raw'] * 640)
    thin = geo['fwidth'] * k < W_PX
    tot = geo['area'].sum() or 1.0
    tex = lod_atlas.Textures(assets)
    out = []
    for i in sorted(tested):
        sel = geo['M'] == i
        if not sel.any():
            continue
        m = mats[i]; sl = body_materials.slots(m)
        holes = None
        try:
            img = tex.get(sl.get('diffuse'))
            if img is not None and np.asarray(img).ndim == 3 and np.asarray(img).shape[2] >= 4:
                a = np.asarray(img)[:, :, 3]
                holes = float(np.mean(a < 128)); mean_a = float(a.mean() / 255)
            else:
                mean_a = None
        except Exception:  # noqa: BLE001
            mean_a = None
        out.append(dict(material=i, blend=flag(m, 'g_AlphaBlendEnable'), zwrite=flag(m, 'g_ZWriteEnable'),
                        area=float(geo['area'][sel].sum() / tot), thin_area=float(geo['area'][sel & thin].sum() / tot),
                        holes=holes, mean_alpha=mean_a, diffuse=(sl.get('diffuse') or b'').decode('latin1')))
    assets.cache.clear()
    return dict(name=name, t_pad=t_pad, faces=int(len(geo['F'])), tested=out,
                tested_area=sum(o['area'] for o in out), thin_tested_area=sum(o['thin_area'] for o in out),
                card_area=sum(o['area'] for o in out if o['holes'] is not None and 0.02 <= o['holes'] <= 0.98),
                thin_card_area=sum(o['thin_area'] for o in out if o['holes'] is not None and 0.02 <= o['holes'] <= 0.98))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--jobs', type=int, default=5); ap.add_argument('--out', type=Path)
    a = ap.parse_args()
    man = manifest_bodies(GAME)
    jobs = [(m['name'], m['t_pad']) for m in man.values()]
    t0 = time.time(); rows = []
    with ProcessPoolExecutor(a.jobs) as ex:
        for r in ex.map(one, jobs, chunksize=4):
            rows.append(r)
    ok = [r for r in rows if 'error' not in r]
    print(f'bodies {len(rows)} ok {len(ok)} errors {len(rows) - len(ok)} wall {time.time() - t0:.0f} s')
    with_t = [r for r in ok if r['tested']]
    cards = [r for r in ok if r.get('card_area', 0) > 0]
    print(f'records with alpha-tested materials: {len(with_t)}; with cutout cards (diffuse alpha holes 2-98 %): {len(cards)}')
    for cat in ('stations/', 'ships/', 'others/'):
        rr = [r for r in ok if r['name'].startswith(cat)]
        wt = [r for r in rr if r['tested']]; wc = [r for r in rr if r.get('card_area', 0) > 0]
        c1 = [r for r in rr if r.get('card_area', 0) > 0.01]; c5 = [r for r in rr if r.get('card_area', 0) > 0.05]
        t1 = [r for r in rr if r.get('thin_card_area', 0) > 0.01]; t5 = [r for r in rr if r.get('thin_card_area', 0) > 0.05]
        print(f'{cat:10s} bodies {len(rr):3d} tested {len(wt):3d} cards {len(wc):3d} card_area>1% {len(c1):3d} >5% {len(c5):3d}'
              f' thin_card_area@s_d>1% {len(t1):3d} >5% {len(t5):3d}')
    zw = sum(1 for r in ok for o in r['tested'] if o['zwrite'] == 1 and o['blend'] == 1)
    zo = sum(1 for r in ok for o in r['tested'] if o['zwrite'] == 0)
    nb = sum(1 for r in ok for o in r['tested'] if o['blend'] == 0)
    print(f'tested materials: blend+zwrite {zw}, zwrite off {zo}, blend off {nb}')
    print('\ntop bodies by thin card area at s_d (name, T_pad, card area %, thin card area %, materials):')
    for r in sorted(cards, key=lambda r: -r['thin_card_area'])[:25]:
        ms = ','.join(f'{o["material"]}:{100 * o["thin_area"]:.1f}%/h{o["holes"]:.2f}' for o in r['tested'] if o['holes'] is not None and 0.02 <= o['holes'] <= 0.98)
        print(f'{r["name"]:60s} {r["t_pad"]:4d} {100 * r["card_area"]:6.2f} {100 * r["thin_card_area"]:6.2f} {ms[:120]}')
    if a.out:
        json.dump(rows, open(a.out, 'w'))


if __name__ == '__main__':
    main()
