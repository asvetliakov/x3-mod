#!/usr/bin/env python3
"""Own-nozzle centre split (run412, Run124 flight G): which part of the own ship's end-on nozzle centre is the
hull's light-map emissive (x hull_lightmap gain 4) and which is the plume.

Only one HDR buffer per F8 frame exists (hdr_1_<frame>.rgba16f, after the native draws and the plume stage), so the
split is spatial: (1) the Scorpion hull's light map (game tree, read-only: split_m4_scorpion\\hull material 0
t_LightMapTexture unique_split_m4_light.dds) sampled at each LOD-0 face's centroid UV; faces with all channels
>= 0.8 are listed with their model-space centroid and normal; (2) per capture frame, the recessed nozzle plate
(flood fill within 0.15 depth units of the depth under the projected fx_engine_xtc_red_nor origin) against an
annulus 3..8 px outside it on hull pixels (depth > 0): mean RGB of both and their difference. The plume disc and
halo are smooth across the plate edge; a term confined to the plate with a sharp edge belongs to the plate's
surface. Reuses run412_engine_disc.py's log reader and projection.

Usage: python3 run412_nozzle_split.py > run412_nozzle_split_out.txt   (~1 min)
"""
import collections
import io
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "analysis"))
import run412_engine_disc as m  # noqa: E402
import bob1  # noqa: E402
import sector_fog_census as sfc  # noqa: E402


def lightmap_faces():
    a = sfc.Assets(Path(bob1.DEFAULT_GAME))
    d, src = a.logical("dds/unique_split_m4_light", (".pck", ".dds", ".tga"))
    lm = np.asarray(Image.open(io.BytesIO(d)).convert("RGB")).astype(float) / 255
    data, prov = bob1.load(r"ships\split\split_m4_scorpion\hull")
    lod = bob1.lods(bob1.parse(data))[0]
    P = np.array([p[1:4] for p in lod["points"]], float)
    UV = np.array([p[4:6] for p in lod["points"]], float) / 65536
    N = np.array([p[6:9] for p in lod["points"]], float) / 65536
    F = np.array([f[:3] for part in lod["parts"] for g in part["groups"] if g["material"] == 0 for f in g["faces"]])
    uv = np.mod(UV[F].mean(1), 1.0)
    val = lm[(uv[:, 1] * (lm.shape[0] - 1)).astype(int), (uv[:, 0] * (lm.shape[1] - 1)).astype(int)]
    cpos, cn = P[F].mean(1), N[F].mean(1)
    print("# light map %s (%s) %dx%d: texels min(rgb)>=0.9 %.4f; hull %s LOD0 %d points, material-0 faces %d (draw 20 primitives 5997), bbox %s..%s"
          % (src["member"], src["source"], lm.shape[1], lm.shape[0], (lm.min(2) >= 0.9).mean(), prov, len(P), len(F),
             P.min(0).astype(int).tolist(), P.max(0).astype(int).tolist()))
    white = np.nonzero(val.min(1) >= 0.8)[0]
    cl = collections.defaultdict(list)
    for i in white:
        cl[tuple((cpos[i] / 3000).round().astype(int))].append(i)
    print("# faces with light map min(rgb) >= 0.8: %d; clusters (faces, model-space centroid, mean normal, light map rgb):" % len(white))
    for _, v in sorted(cl.items(), key=lambda kv: -len(kv[1])):
        v = np.array(v)
        print("  %3d pos %s n %s lm %s" % (len(v), cpos[v].mean(0).round(0).tolist(), cn[v].mean(0).round(2).tolist(),
                                          val[v].mean(0).round(2).tolist()))


def plate_split(fr, cam, origin, rad=60):
    x, y, z = m.project(cam, origin)
    cx, cy = int(round(x)), int(round(y))
    y0, x0 = cy - rad, cx - rad
    hdr = np.memmap(os.path.join(m.SESSION, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(m.H, m.W, 4))
    e = np.array(hdr[y0:y0 + 2 * rad + 1, x0:x0 + 2 * rad + 1, :3]).astype(float)
    del hdr
    dep = np.memmap(os.path.join(m.SESSION, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(m.H, m.W, 4))
    zz = np.array(dep[y0:y0 + 2 * rad + 1, x0:x0 + 2 * rad + 1, 2]).astype(float)
    del dep
    d0 = zz[rad, rad]
    plate = m.flood((np.abs(zz - d0) < 0.15) & (zz > 0), rad, rad)
    # distance to the plate (chessboard dilation steps)
    dist = np.full(plate.shape, 99)
    cur = plate.copy()
    for k in range(1, 9):
        nxt = cur.copy()
        nxt[1:] |= cur[:-1]; nxt[:-1] |= cur[1:]; nxt[:, 1:] |= cur[:, :-1]; nxt[:, :-1] |= cur[:, 1:]
        dist[nxt & ~cur] = k
        cur = nxt
    ring = (dist >= 3) & (dist <= 8) & (zz > 0)
    pin, pout = e[plate].mean(0), e[ring].mean(0)
    edge_in = e[plate & ~(m.flood(plate, rad, rad) & np.roll(plate, 1, 0) & np.roll(plate, -1, 0) & np.roll(plate, 1, 1) & np.roll(plate, -1, 1))]
    edge_out = e[(dist == 1) & (zz > 0)]
    return dict(z=z, origin_px=(x, y), plate_px=int(plate.sum()), plate_depth=d0, plate=pin, plate_min=e[plate].min(0),
                ring=pout, ring_px=int(ring.sum()), edge_in=edge_in.mean(0), edge_out=edge_out.mean(0), centre=e[rad, rad])


def main():
    lightmap_faces()
    cams, evs, draws = m.read_log()
    print("# per frame, nozzle fx_engine_xtc_red_nor: origin view z, plate px (depth under the origin), plate mean RGB,"
          " plate min RGB, plate edge row (inner 1 px) RGB | 1 px outside RGB | annulus 3..8 px outside (hull) RGB, plate - annulus")
    for fr in m.FRAMES:
        for name, size, p in draws.get(fr, []):
            if not name.endswith("_nor"):
                continue
            s = plate_split(fr, cams[fr], p)
            f3 = lambda v: "%.2f/%.2f/%.2f" % tuple(v)
            print("%d z=%.1f d=%.2f plate %3d px mean %s min %s | edge in %s out %s | ring(%d) %s | diff %s | centre %s ev=%.2f"
                  % (fr, s["z"], s["plate_depth"], s["plate_px"], f3(s["plate"]), f3(s["plate_min"]), f3(s["edge_in"]),
                     f3(s["edge_out"]), s["ring_px"], f3(s["ring"]), f3(s["plate"] - s["ring"]), f3(s["centre"]),
                     evs.get(fr - 1, evs[fr])))


if __name__ == "__main__":
    main()
