#!/usr/bin/env python3
"""Per F8 frame: EV (frame before), engine_stage counters, and per engine_draw record its view distance, facing
f = axis . to_camera, projected nozzle width n = 0.5 value_eff px (x ppu), the stage's detail level, disc weight
smoothstep(0.3, 0.7, |f|), axial weight 1 - 0.5 disc, disc distance factor (src/proxy/engine_plumes_core.h laws).
Usage: python3 run416_geometry.py run416|run414 [frame ...]
"""
import json, math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
RUN = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].startswith("run") else "run416"
L = json.load(open(os.path.join(HERE, RUN + "_log.json")))
W, H = 5120, 1440
def sm(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0); return t * t * (3 - 2 * t)
def cam(d):
    r = np.array([[float(d["r%d%d" % (i, j)]) for j in range(3)] for i in range(3)])
    return r, np.array([float(x) for x in d["t"].split(",")]), float(d["p00"]), float(d["p11"])
def nozzles(fr):
    s = L[str(fr)]; r, t, p00, p11 = cam(s["camera_state"]); out = []
    for d in s.get("engine_draw", []):
        o = np.array([float(x) for x in d["origin"].split(",")]); a = np.array([float(x) for x in d["axis"].split(",")])
        v = o @ r + t; av = a @ r; e = -v / np.linalg.norm(v); f = float(av @ e)
        x = (v[0] / v[2] * p00 * 0.5 + 0.5) * W; y = (0.5 - v[1] / v[2] * p11 * 0.5) * H
        value = float(d["value_eff"]); ppu = p00 * 0.5 * W / v[2]; npx = max(0.5 * value * ppu, 2.0)
        dw = sm(0.3, 0.7, abs(f))
        out.append(dict(name=d["name"].replace("fx_engine_xtc_", ""), handle=d["handle"], verdict=d.get("verdict"), x=x, y=y,
                        vz=float(v[2]), dist=float(np.linalg.norm(v)), f=f, value=value, size=float(d["size"]), z=float(d["z"]), s=float(d["s"]),
                        npx=npx, detail=sm(8, 40, 0.5 * value * ppu), disc_w=dw, axial_w=1 - 0.5 * dw,
                        disc_far=0.5 + 0.5 * sm(20, 160, npx), far=0.15 + 0.85 * sm(2, 12, 0.5 * value * ppu)))
    return out
if __name__ == "__main__":
    frames = [int(a) for a in sys.argv[2:]] or sorted(int(k) for k in L if "engine_draw" in L[k])
    for fr in frames:
        s = L[str(fr)]
        print("## %d ev(fr-1) %s stage %s" % (fr, L.get(str(fr - 1), {}).get("hdr_frame", {}).get("ev_adapted"), s.get("engine_stage")))
        for n in nozzles(fr):
            print("  %-8s h=%s %-10s px(%.0f,%.0f) dist %.0f f %+.3f value %.1f(size %.1f) z %.2f s %.2f n %.1f px detail %.2f disc_w %.2f axial_w %.2f disc_far %.2f far %.2f" % (
                n["name"], n["handle"], n["verdict"], n["x"], n["y"], n["dist"], n["f"], n["value"], n["size"], n["z"], n["s"], n["npx"], n["detail"], n["disc_w"], n["axial_w"], n["disc_far"], n["far"]))
