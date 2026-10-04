#!/usr/bin/env python3
"""Nozzle-plate light-map suppression (docs/architecture/engine-light.md "Nozzle plates"): which faces of the Split
Scorpion's hull the suppression weight of the engine-light twin covers, the white rear nozzle plates (light map
min(rgb) >= 0.8, run412_nozzle_split.py) against every other light-mapped face (windows, markings).

The light of the flight (run412, Run124 flight G, frame 5437): the own ship's brightest main nozzle
fx_engine_xtc_red_nor, value 10, value_eff 23.5 after the plume floor; its world origin (engine_draw origin) taken
into the hull's model space through that frame's world rows of the hull draw (draw 20, primitives 5997, object_matrix
role=world). The light sits behind x value_eff along the axis (engine_light_core.h, behind 0.5); d is measured from
it, as in the pixel program. Model units: the bob's integer coordinates (1/65536 of the VS input; world row norm 62.44,
so 1 record unit = 1,049.6 model units).

Weight shapes: w = saturate((r1^2 - (d/v)^2) / (r1^2 - r0^2)) (r0 = 0: the plain saturate(1 - d^2 / R_lm^2) with
R_lm = r1 v), gain_eff = gain - (gain - 1) w at gain 4. Game tree read-only. Usage:
python3 nozzle_plate_coverage.py > nozzle_plate_coverage_out.txt
"""
import io
import os
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "analysis"))
import bob1  # noqa: E402
import sector_fog_census as sfc  # noqa: E402

# run412 session log, frame 5437 (draw 20 world rows as float bits; engine_draw fx_engine_xtc_red_nor).
WORLD = ["c20248b5,c24d52c2,c163ea43", "c2064823,4202f852,c224e748", "422571d2,c15d8eeb,c232b6f2",
         "47bc5f13,c7128e9e,4695b9de"]
NOZZLE = np.array([96411.5, -37509.1, 19210.0])
AXIS = np.array([-0.66242, 0.22177, 0.71555])
VALUE_EFF, BEHIND, GAIN = 23.5, 0.5, 4.0
SHAPES = [(0.75, 1.0), (0.0, 1.25), (0.0, 2.0), (0.75, 1.25)]   # the first is the shipped rule (r0 full, r1 zero)


def f32(h):
    return struct.unpack("<f", struct.pack("<I", int(h, 16)))[0]


def main():
    np.seterr(all="ignore")  # numpy's BLAS matmul raises spurious FP flags on this host; the values are finite
    w = np.array([[f32(x) for x in row.split(",")] for row in WORLD])
    a, t = w[:3], w[3]
    inv = np.linalg.inv(a)
    nozzle = (NOZZLE - t) @ inv * 65536
    axis = AXIS @ inv
    axis /= np.linalg.norm(axis)
    unit = 65536 / np.linalg.norm(a, axis=1).mean()          # model units per record unit
    v = VALUE_EFF * unit
    light = nozzle + axis * BEHIND * v
    print("# model units per record unit %.1f; nozzle model %s axis %s; value_eff %.1f = %.0f model units; light %s"
          % (unit, nozzle.round(0).tolist(), axis.round(3).tolist(), VALUE_EFF, v, light.round(0).tolist()))
    assets = sfc.Assets(Path(bob1.DEFAULT_GAME))
    d, _ = assets.logical("dds/unique_split_m4_light", (".pck", ".dds", ".tga"))
    lm = np.asarray(Image.open(io.BytesIO(d)).convert("RGB")).astype(float) / 255
    data, _ = bob1.load(r"ships\split\split_m4_scorpion\hull")
    lod = bob1.lods(bob1.parse(data))[0]
    p = np.array([q[1:4] for q in lod["points"]], float)
    uv = np.array([q[4:6] for q in lod["points"]], float) / 65536
    faces = np.array([f[:3] for part in lod["parts"] for g in part["groups"] if g["material"] == 0 for f in g["faces"]])
    c = np.mod(uv[faces].mean(1), 1.0)
    val = lm[(c[:, 1] * (lm.shape[0] - 1)).astype(int), (c[:, 0] * (lm.shape[1] - 1)).astype(int)]
    pos = p[faces].mean(1)
    area = 0.5 * np.linalg.norm(np.cross(p[faces[:, 1]] - p[faces[:, 0]], p[faces[:, 2]] - p[faces[:, 0]]), axis=1)
    dv = np.linalg.norm(pos - light, axis=1) / v
    # the faces' farthest vertex too (a face straddling the edge of the full-suppression sphere)
    dv_max = np.max(np.linalg.norm(p[faces] - light, axis=2), axis=1) / v
    plate = val.min(1) >= 0.8
    lit = (val.max(1) >= 0.2) & ~plate
    ahead = (pos - nozzle) @ (-axis)                             # forward of the nozzle along the hull, model units
    print("# faces %d; plates (lm min >= 0.8) %d: d/v centroid %.3f..%.3f, farthest vertex %.3f; along-axis offset from"
          " the nozzle %.0f..%.0f model units" % (len(faces), plate.sum(), dv[plate].min(), dv[plate].max(),
                                                 dv_max[plate].max(), ahead[plate].min(), ahead[plate].max()))
    print("# other light-mapped faces (lm max >= 0.2) %d, lm max mean %.2f; nearest d/v %.3f; within d/v < 1.0 / 1.25 /"
          " 1.5 / 2.0: %d / %d / %d / %d" % (lit.sum(), val[lit].max(1).mean(), dv[lit].min(), (dv[lit] < 1.0).sum(),
                                            (dv[lit] < 1.25).sum(), (dv[lit] < 1.5).sum(), (dv[lit] < 2.0).sum()))
    order = np.argsort(dv[lit])[:12]
    for i in np.nonzero(lit)[0][order]:
        print("  lit face d/v %.3f pos %s lm %s area %.0f" % (dv[i], pos[i].round(0).tolist(), val[i].round(2).tolist(),
                                                               area[i]))
    print("# shape (r0, r1): plate gain_eff min / mean (area-weighted) / max at gain %.0f | other lit faces touched"
          " (w > 0), their light-mapped area share, the mean gain_eff over them" % GAIN)
    for r0, r1 in SHAPES:
        wt = np.clip((r1 * r1 - dv * dv) / (r1 * r1 - r0 * r0), 0.0, 1.0)
        g = GAIN - (GAIN - 1.0) * wt
        touched = lit & (wt > 0)
        share = (area[touched] * val[touched].max(1)).sum() / max((area[lit] * val[lit].max(1)).sum(), 1e-9)
        stern = touched & (dv < 0.6)
        fore = touched & ~stern
        print("  (%.2f, %.2f): plates %.3f / %.3f / %.3f | lit %d, %.4f of the lit area x lm, gain_eff %s; of them the"
              " nozzle surround (d/v < 0.6, z %s) %d at gain_eff %s, further forward %d (z %s, lm max %s) at gain_eff %s"
              % (r0, r1, g[plate].min(), (g[plate] * area[plate]).sum() / area[plate].sum(), g[plate].max(),
                 touched.sum(), share, ("%.2f" % g[touched].mean()) if touched.any() else "-",
                 "%.0f..%.0f" % (pos[stern, 2].min(), pos[stern, 2].max()) if stern.any() else "-", stern.sum(),
                 ("%.2f" % g[stern].mean()) if stern.any() else "-", fore.sum(),
                 "%.0f..%.0f" % (pos[fore, 2].min(), pos[fore, 2].max()) if fore.any() else "-",
                 ("%.2f" % val[fore].max(1).max()) if fore.any() else "-",
                 ("%.2f..%.2f" % (g[fore].min(), g[fore].max())) if fore.any() else "-"))


if __name__ == "__main__":
    main()
