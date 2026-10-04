#!/usr/bin/env python3
"""Run 125 A (run413): the Split Ocelot hull's faces around each of its engine records, with their materials and the
texture values a nozzle shows (the light map x hull_lightmap gain 4 for light-mapped materials; material 11's
diffuse metal_split_ExL_01_light x emissive 1, its own program, hull_emission gain 1).

The Ocelot hull draw's world rows (object_matrix role=world after the first draw of node 2902a2c0 in the frame) map
each engine_draw origin and axis into model space (model unit = VS input x 65536; 1 record unit = 65536 / |row|).
Per record: the faces (LOD 0) whose centroid lies within 0.45 value_eff of the origin (record units, radial r from the
axis and axial a along it, the exhaust +), grouped by material and by the face normal's alignment with the axis
(plate: |n.a| >= 0.8; rim/wall: |n.a| < 0.5), each with face count, r and a ranges in value_eff, the mean texture RGB
at the centroid UV (mod 1), and the distance d/v from the ship's engine light (0.5 value_eff behind the lit nozzle: the
lower node handle of the two red_huge records, engine_light_core.h build_ships tie rule), with gain_eff = 4 - 3 w
(w = saturate((1 - (d/v)^2) / (1 - 0.75^2))) for light-mapped materials. Game tree read-only.
Usage: python3 run413_ocelot_faces.py 6044 > run413_ocelot_faces_6044_out.txt
"""
import collections, glob, io, json, os, struct, sys
from pathlib import Path
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "analysis"))
import bob1  # noqa: E402
import sector_fog_census as sfc  # noqa: E402
LOG = glob.glob("/tmp/x3-bottleX3-run413/session-*.log")[0]
L = json.load(open(os.path.join(HERE, "run413_log.json")))
NODE = b"node=2902a2c0"


def world_rows(fr):
    pre = b"draw device=1 frame=%d " % fr
    state, rows = 0, {}
    with open(LOG, "rb") as f:
        for raw in f:
            if state == 0 and raw.startswith(pre):
                state = 1
            elif state == 1 and raw.startswith(b"object_context ") and NODE in raw:
                state = 2
            elif state == 1 and raw.startswith(b"draw device=1 "):
                state = 1 if raw.startswith(pre) else 0
            elif state == 2 and raw.startswith(b"object_matrix role=world row="):
                k = int(raw.split(b"row=")[1].split()[0])
                rows[k] = [struct.unpack("<f", struct.pack("<I", int(h, 16)))[0] for h in raw.split(b"bits=")[1].decode().split(",")[:3]]
                if k == 3:
                    return np.array([rows[i] for i in range(4)])
    raise SystemExit("no world rows")


def main(fr):
    np.seterr(all="ignore")
    w = world_rows(fr); A, t = w[:3], w[3]; inv = np.linalg.inv(A)
    unit = 65536 / np.linalg.norm(A, axis=1).mean()
    assets = sfc.Assets(Path(bob1.DEFAULT_GAME))
    data, prov = bob1.load(r"ships\split\split_m2p_ocelot\hull")
    tree = bob1.parse(data)
    tex = {}
    for m in bob1.materials(tree):
        p = {k.decode(): v for k, _, v in m["params"]}
        lm = p["t_LightMapTexture"].decode(); df = p["t_DiffuseTexture"].decode()
        name, kind = (lm, "lightmap") if lm != "NULL" else ((df, "emissive") if m["index"] == 11 else (None, None))
        if name and not name.startswith("x3m_lod"):   # material 13: the x3m LOD overlay's (coarse LODs only)
            d, _ = assets.logical("dds/" + name.rsplit(".", 1)[0], (".pck", ".dds", ".tga"))
            tex[m["index"]] = (kind, name, np.asarray(Image.open(io.BytesIO(d)).convert("RGB")).astype(float) / 255)
    lod = bob1.lods(tree)[0]
    P = np.array([p[1:4] for p in lod["points"]], float)
    UV = np.array([p[4:6] for p in lod["points"]], float) / 65536
    N = np.array([p[6:9] for p in lod["points"]], float) / 65536
    F, M = [], []
    for part in lod["parts"]:
        for g in part["groups"]:
            for fc in g["faces"]:
                F.append(fc[:3]); M.append(g["material"])
    F = np.array(F); M = np.array(M)
    C = P[F].mean(1); Nf = N[F].mean(1); Nf /= np.maximum(np.linalg.norm(Nf, axis=1, keepdims=True), 1e-9)
    uv = np.mod(UV[F].mean(1), 1.0)
    val = np.zeros((len(F), 3))
    for mi, (kind, name, img) in tex.items():
        s = M == mi
        val[s] = img[(uv[s, 1] * (img.shape[0] - 1)).astype(int), (uv[s, 0] * (img.shape[1] - 1)).astype(int)]
    print("# frame %d Ocelot %s LOD0 points %d faces %d; model units per record unit %.1f; textures: %s" % (
        fr, prov, len(P), len(F), unit, ", ".join("%d %s %s" % (k, v[0], v[1]) for k, v in sorted(tex.items()))))
    draws = [d for d in L[str(fr)]["engine_draw"] if "red_huge" in d["name"] or "red_big3" in d["name"]]
    huge = sorted([d for d in draws if "red_huge" in d["name"]], key=lambda d: int(d["handle"], 16))
    lit = huge[0]
    lo = (np.array([float(x) for x in lit["origin"].split(",")]) - t) @ inv * 65536
    la = np.array([float(x) for x in lit["axis"].split(",")]) @ inv; la /= np.linalg.norm(la)
    lv = float(lit["value_eff"]) * unit
    light = lo + la * 0.5 * lv
    print("# engine light: record h=%s idx=%s (red_huge, lower handle of the tie), light at model %s, value_eff %.1f = %.0f model units"
          % (lit["handle"], lit["index"], light.round(0).tolist(), float(lit["value_eff"]), lv))
    for d in draws:
        o = (np.array([float(x) for x in d["origin"].split(",")]) - t) @ inv * 65536
        a = np.array([float(x) for x in d["axis"].split(",")]) @ inv; a /= np.linalg.norm(a)
        v = float(d["value_eff"]) * unit
        rel = C - o; ax = rel @ a; rad = np.linalg.norm(rel - np.outer(ax, a), axis=1)
        near = np.nonzero(np.hypot(ax, rad) < 0.45 * v)[0]
        dl = np.linalg.norm(C - light, axis=1) / lv
        wgt = np.clip((1 - dl ** 2) / (1 - 0.75 ** 2), 0, 1)
        print("## %s h=%s idx=%s value_eff %.1f (size %s) origin model %s axis %s: %d faces within 0.45 value" % (
            d["name"].split("_")[-1], d["handle"], d["index"], float(d["value_eff"]), d["size"], o.round(0).tolist(), a.round(2).tolist(), len(near)))
        groups = collections.defaultdict(list)
        for i in near:
            na = abs(Nf[i] @ a)
            kind = "plate" if na >= 0.8 else ("wall" if na < 0.5 else "slant")
            groups[(M[i], kind)].append(i)
        for (mi, kind), ids in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1])):
            ids = np.array(ids)
            tk = tex.get(mi, ("none",))[0]
            ge = (4 - 3 * wgt[ids]) if tk == "lightmap" else np.ones(len(ids))
            print("  mat %2d %-8s %-5s faces %4d r/v %.3f..%.3f a/v %+.3f..%+.3f tex mean %s max %s | d/v light %.2f..%.2f gain_eff %.2f..%.2f -> tex x gain mean %s" % (
                mi, tk, kind, len(ids), rad[ids].min() / v, rad[ids].max() / v, ax[ids].min() / v, ax[ids].max() / v,
                val[ids].mean(0).round(2).tolist(), val[ids].max(0).round(2).tolist(), dl[ids].min(), dl[ids].max(), ge.min(), ge.max(),
                (val[ids] * ge[:, None]).mean(0).round(2).tolist()))


if __name__ == "__main__":
    main(int(sys.argv[1]))
