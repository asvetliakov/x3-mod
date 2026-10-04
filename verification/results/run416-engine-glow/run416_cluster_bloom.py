#!/usr/bin/env python3
"""The Ocelot's end-on nozzle cluster: where its HDR energy sits, and what the bloom adds on the display.

Captures read-only, one frame at a time (memory-mapped; a crop with its origin and size on the 32 px grid, so the
crop's bloom chain lines up with the full frame's five ceil-half levels; >= 256 px margin about the cluster).
Classes over the cluster region (pixels within 2.5 n_i of any Ocelot nozzle, red_big3 / red_huge; n_i = 0.5 value_eff
ppu): DISC = within 0.75 n_i of some nozzle (the fixture 02b disc's whole footprint: 0 % of its energy past 0.75 n,
run416_profiles_fixture02b_out.txt); outside every footprint: SKY (depth -1), PLATE (scene alpha >= 0.5: the game's
authored-glow mask on the hull), HULL (the rest). Energy = sum of decoded luma (engine^2.2, Rec.709) less the class
background (SKY: median decoded luma of sky pixels 3-5 n_huge from the cluster; HULL / PLATE: none subtracted).

Bloom (src/temporal/bloom_*.hlsl, live parameters src/proxy/capture.cpp:1508-1514, BloomParams defaults
src/temporal/bloom.h:16): levels 5, strength 1, threshold 1 (exposed luma), knee 0.5, scatter 0.65, authored-glow gain
0.375 x alpha, highlight gain 0.05 x (1 - alpha) x the threshold weight, source clamp 1.0 decoded (X3M_BLOOM_SOURCE_CLAMP
1.0@default in the log); extraction: min(decoded, 1) x exposure; 2x2 area down to /32; up: lerp(fine, tent(coarse),
scatter); full res tent; composite: decoded x exposure + bloom, then AgX (run412_engine_disc.tonemap's core). Exposure
= 2^ev_adapted of the frame before. The bloom is linear in its prefiltered source, so it is split by source:
authored term (alpha) vs highlight term, and by the source pixel's class (DISC / outside).
Display metrics per class: mean display luma (Rec.709 of the AgX output) without and with bloom; and "glow" pixels
(display R >= 0.5) outside every disc footprint, without / with bloom.
Usage: python3 run416_cluster_bloom.py RUN FRAME [--png DIR]
"""
import math, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, "..", "run412-engine-disc"))
import run412_engine_disc as m  # noqa: E402
W, H = 5120, 1440
LW = np.array([0.2126, 0.7152, 0.0722])
def agx_exposed(v):
    with np.errstate(all="ignore"):
        v = np.maximum(v, 0) @ m.M_IN.T
        t = (np.clip(np.log2(np.maximum(v, m.agx.LOG_FLOOR)), m.agx.MIN_EV, m.agx.MAX_EV) - m.agx.MIN_EV) / (m.agx.MAX_EV - m.agx.MIN_EV)
        return np.clip(np.polyval(m.COEF, t) @ m.M_OUT.T, 0.0, 1.0)
def prefilter(c4, exposure, split):
    e = np.minimum(np.maximum(c4[..., :3], 0) ** 2.2, 1.0) * exposure
    y = e @ LW; t, k = 1.0, 0.5
    q = np.clip(y - t + k, 0, 2 * k); soft = q * q / (4 * k)
    w = np.clip(np.maximum(y - t, soft) / np.maximum(y, 1e-10), 0, 1)
    a = np.clip(c4[..., 3], 0, 1)
    authored = e * (a * 0.375)[..., None]; highlight = e * ((1 - a) * 0.05 * w)[..., None]
    return authored if split == "authored" else highlight if split == "highlight" else authored + highlight
def down(img):
    return 0.25 * (img[0::2, 0::2] + img[1::2, 0::2] + img[0::2, 1::2] + img[1::2, 1::2])
def bilinear(img, xs, ys):
    sh, sw = img.shape[:2]
    x0 = np.floor(xs).astype(int); y0 = np.floor(ys).astype(int); fx = xs - x0; fy = ys - y0
    xa = np.clip(x0, 0, sw - 1); xb = np.clip(x0 + 1, 0, sw - 1); ya = np.clip(y0, 0, sh - 1); yb = np.clip(y0 + 1, 0, sh - 1)
    top = img[ya][:, xa] * (1 - fx)[None, :, None] + img[ya][:, xb] * fx[None, :, None]
    bot = img[yb][:, xa] * (1 - fx)[None, :, None] + img[yb][:, xb] * fx[None, :, None]
    return top * (1 - fy)[:, None, None] + bot * fy[:, None, None]
def tent(img, h, w):
    sh, sw = img.shape[:2]; out = 0
    xs0 = (np.arange(w) + .5) * sw / w - .5; ys0 = (np.arange(h) + .5) * sh / h - .5
    for dx, wx in ((-1, .25), (0, .5), (1, .25)):
        for dy, wy in ((-1, .25), (0, .5), (1, .25)):
            out = out + bilinear(img, xs0 + dx, ys0 + dy) * (wx * wy)
    return out
def bloom(src, levels=5, scatter=0.65):
    chain = [src]
    for _ in range(levels):
        chain.append(down(chain[-1]))
    chain = chain[1:]; cur = chain[-1]
    for fine in reversed(chain[:-1]):
        cur = (1 - scatter) * fine + scatter * tent(cur, *fine.shape[:2])
    return tent(cur, *src.shape[:2])
def main(run, fr, png):
    np.seterr(all="ignore")
    sys.argv = [sys.argv[0], run]
    import run416_geometry as g
    ev = float(g.L[str(fr - 1)]["hdr_frame"]["ev_adapted"]); exposure = 2.0 ** ev
    noz = [n for n in g.nozzles(fr) if n["name"] in ("red_big3", "red_huge")]
    nh = max(n["npx"] for n in noz)
    xs = [n["x"] for n in noz]; ys = [n["y"] for n in noz]
    x0 = max(0, int((min(xs) - 5 * nh - 256) // 32 * 32)); y0 = max(0, int((min(ys) - 5 * nh - 256) // 32 * 32))
    x1 = min(W, int(math.ceil((max(xs) + 5 * nh + 256) / 32) * 32)); y1 = min(H, int(math.ceil((max(ys) + 5 * nh + 256) / 32) * 32))
    sess = "/tmp/x3-bottleX3-" + run
    hdr = np.memmap(os.path.join(sess, "hdr_1_%d.rgba16f" % fr), dtype="<f2", mode="r", shape=(H, W, 4))
    dep = np.memmap(os.path.join(sess, "depth_1_%d.rgba32f" % fr), dtype="<f4", mode="r", shape=(H, W, 4))
    c4 = np.array(hdr[y0:y1, x0:x1]).astype(np.float64); z = np.array(dep[y0:y1, x0:x1, 2]).astype(np.float64)
    del hdr, dep
    yy, xx = np.mgrid[y0:y1, x0:x1]
    rmin = np.full(z.shape, np.inf); region = np.zeros(z.shape, bool); disc = np.zeros(z.shape, bool); dmin_h = np.full(z.shape, np.inf)
    for n in noz:
        r = np.hypot(xx - n["x"], yy - n["y"])
        disc |= r <= 0.75 * n["npx"]; region |= r <= 2.5 * n["npx"]
        dmin_h = np.minimum(dmin_h, r / nh)
    sky = z <= 0; alpha = c4[..., 3]
    plate = ~disc & ~sky & (alpha >= .5); hull = ~disc & ~sky & (alpha < .5); osky = ~disc & sky
    lin = np.maximum(c4[..., :3], 0) ** 2.2; ll = lin @ LW
    bg_sky = float(np.median(ll[sky & (dmin_h >= 3) & (dmin_h <= 5)]))
    print("## %s frame %d crop x %d..%d y %d..%d ev(frame-1) %.4f exposure %.4f; %d Ocelot nozzles, n_huge %.1f px; sky background decoded luma %.5f" % (
        run, fr, x0, x1, y0, y1, ev, exposure, len(noz), nh, bg_sky))
    tot = 0; rows = []
    for name, msk, bg in (("DISC", disc & region, 0.0), ("SKY", osky & region, bg_sky), ("HULL", hull & region, 0.0), ("PLATE", plate & region, 0.0)):
        en = float(np.maximum(ll[msk] - bg, 0).sum()); tot += en
        rows.append((name, int(msk.sum()), en, lin[msk].mean(0) if msk.any() else np.zeros(3)))
    print("HDR energy in the cluster region (decoded luma, background-subtracted):")
    for name, cnt, en, rgb in rows:
        print("  %-5s px %7d  energy %10.1f  %5.1f%%  mean decoded RGB %.3f %.3f %.3f" % (name, cnt, en, 100 * en / tot, *rgb))
    # Bloom, split by source term and by source class.
    parts = {}
    for split in ("authored", "highlight"):
        s = prefilter(c4, exposure, split)
        parts[split + "/disc"] = bloom(s * disc[..., None]); parts[split + "/out"] = bloom(s * (~disc)[..., None])
    b = sum(parts.values())
    scene = np.minimum(lin, 65504) * exposure
    d0 = agx_exposed(scene); d1 = agx_exposed(scene + b)
    srcsum = {k: float(prefilter(c4, exposure, k.split("/")[0])[(disc if k.endswith("disc") else ~disc)].sum(0) @ LW) for k in parts}
    tsrc = sum(srcsum.values())
    print("bloom source (prefiltered exposed luma, whole crop) by term/class: " + ", ".join("%s %.1f (%.1f%%)" % (k, v, 100 * v / tsrc) for k, v in srcsum.items()))
    print("bloom added over the cluster region's classes (mean exposed luma of the bloom term, its share by source; scene exposed luma beside):")
    for name, msk in (("DISC", disc & region), ("SKY", osky & region), ("HULL", hull & region), ("PLATE", plate & region)):
        bl = {k: float((v[msk] @ LW).mean()) for k, v in parts.items()}; tb = sum(bl.values())
        sc = float((scene[msk] @ LW).mean())
        print("  %-5s bloom %.4f (scene %.4f, bloom/scene %.2f) | " % (name, tb, sc, tb / sc if sc else float("nan")) +
              ", ".join("%s %.0f%%" % (k, 100 * v / tb) for k, v in bl.items()))
    print("display (AgX) mean luma and glow pixels (display R >= 0.5), without -> with bloom:")
    for name, msk in (("DISC", disc & region), ("SKY", osky & region), ("HULL", hull & region), ("PLATE", plate & region)):
        print("  %-5s mean luma %.3f -> %.3f   R>=0.5 px %6d -> %6d   R>=0.8 px %6d -> %6d" % (
            name, float((d0[msk] @ LW).mean()), float((d1[msk] @ LW).mean()),
            int((d0[msk][:, 0] >= .5).sum()), int((d1[msk][:, 0] >= .5).sum()), int((d0[msk][:, 0] >= .8).sum()), int((d1[msk][:, 0] >= .8).sum())))
    # Outside the region (sky ring 2.5-4 n_huge from the cluster).
    ring = sky & (dmin_h > 2.5) & (dmin_h <= 4)
    print("  sky ring 2.5-4 n_huge outside the region: mean display luma %.3f -> %.3f; R>=0.5 px %d -> %d" % (
        float((d0[ring] @ LW).mean()), float((d1[ring] @ LW).mean()), int((d0[ring][:, 0] >= .5).sum()), int((d1[ring][:, 0] >= .5).sum())))
    # Connected groups (4-neighbour) of glow pixels in the region, and which nozzles each holds.
    def groups(mask):
        lab = np.zeros(mask.shape, np.int32); cur = 0; sizes = []
        for sy, sx in zip(*np.nonzero(mask)):
            if lab[sy, sx]:
                continue
            cur += 1; stack = [(sy, sx)]; lab[sy, sx] = cur; cnt = 0
            while stack:
                cy, cx = stack.pop(); cnt += 1
                for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                    if 0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1] and mask[ny, nx] and not lab[ny, nx]:
                        lab[ny, nx] = cur; stack.append((ny, nx))
            sizes.append(cnt)
        held = {}
        for nz in noz:
            l = lab[int(round(nz["y"])) - y0, int(round(nz["x"])) - x0]
            held.setdefault(int(l), []).append(nz["name"].replace("red_", "") + ":" + nz["handle"][-2:])
        return ", ".join("%d px {%s}" % (sizes[l - 1], " ".join(v)) for l, v in sorted(held.items()) if l) + \
               ("; nozzles outside any group: %s" % " ".join(held[0]) if 0 in held else "")
    print("connected groups holding the nozzles (4-neighbour, inside the region):")
    print("  disc footprints (0.75 n_i): " + groups(disc & region))
    for thr in (0.5, 0.8):
        print("  display R >= %.1f without bloom: " % thr + groups((d0[..., 0] >= thr) & region))
        print("  display R >= %.1f with bloom:    " % thr + groups((d1[..., 0] >= thr) & region))
    print("  DISC display white (min >= 0.98) px %d -> %d of %d; R >= 0.98 px %d -> %d" % (
        int((d0[disc & region].min(1) >= .98).sum()), int((d1[disc & region].min(1) >= .98).sum()), int((disc & region).sum()),
        int((d0[disc & region][:, 0] >= .98).sum()), int((d1[disc & region][:, 0] >= .98).sum())))
    if png:
        from PIL import Image
        cx0 = int(min(xs) - 2.5 * nh) - x0; cy0 = int(min(ys) - 2.5 * nh) - y0; cx1 = int(max(xs) + 2.5 * nh) - x0; cy1 = int(max(ys) + 2.5 * nh) - y0
        for tag, img in (("nobloom", d0), ("bloom", d1), ("bloomonly", agx_exposed(b))):
            Image.fromarray((img[cy0:cy1, cx0:cx1] * 255).astype(np.uint8)).save(os.path.join(png, "%s_%d_%s.png" % (run, fr, tag)))
        cls = np.zeros(z.shape + (3,), np.uint8); cls[disc & region] = (255, 0, 0); cls[osky & region] = (0, 0, 255)
        cls[hull & region] = (0, 255, 0); cls[plate & region] = (255, 255, 0)
        Image.fromarray(cls[cy0:cy1, cx0:cx1]).save(os.path.join(png, "%s_%d_classes.png" % (run, fr)))
if __name__ == "__main__":
    a = sys.argv[1:]
    png = a[a.index("--png") + 1] if "--png" in a else None
    main(a[0], int(a[1]), png)
