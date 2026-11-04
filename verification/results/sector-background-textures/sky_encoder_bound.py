"""Why answer 4's run and hue limits are not met together on real parts (sky bake, 2026-11-04).

  python3 verification/results/sector-background-textures/sky_encoder_bound.py --game /mnt/c/X3FL

(1) Encoder quality (invariant 8): lod_atlas._encode_color (PCA, 4-colour) on the 2x Catmull-Rom of the stock
    bluewell/avdustysol part 01, encoder PSNR over non-zero texels and time, and the same with one least-squares
    endpoint refinement iteration (fixed indices, re-quantised, kept per block where the error drops).
(2) The run limit on an exact image: the smooth blue ramp of test_sky_textures.banded_part without its banding,
    rounded to 8 bits, has a mean identical-luma run far above 1.5 px (smooth gradients slower than about 1 level per
    px repeat 8-bit values).
(3) A bound looser than DXT1: every texel any RGB565 colour, per-channel ordered 8x8 Bayer dither of +-0.5 and +-1
    565 step at bake resolution and at source resolution (2x2 cells), on the hue-locked debanded and upsampled bluewell
    part 01 (a 512x1024 crop): run and hue (sky_textures.metrics definitions) cannot both meet their limits.
"""
import argparse
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
sys.path.insert(0, str(ROOT / 'verification' / 'analysis'))
import lod_atlas  # noqa: E402
import sky_textures as st  # noqa: E402


def refine(rgb, blk):
    """One least-squares endpoint iteration for the indices the PCA fit chose."""
    w0 = np.array([1.0, 0.0, 2 / 3, 1 / 3], np.float32)
    c0 = blk[:, 0].astype(np.uint32) | blk[:, 1].astype(np.uint32) << 8
    c1 = blk[:, 2].astype(np.uint32) | blk[:, 3].astype(np.uint32) << 8
    idx = (blk[:, 4:8].copy().view('<u4')[:, 0][:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3
    a = w0[idx]
    b = 1 - a
    aa, bb, ab = (a * a).sum(1), (b * b).sum(1), (a * b).sum(1)
    ax, bx = np.einsum('nk,nkc->nc', a, rgb), np.einsum('nk,nkc->nc', b, rgb)
    det = aa * bb - ab * ab
    ok = (np.abs(det) > 1e-6) & (c0 > c1)
    det = np.where(ok, det, 1.0)
    n0 = lod_atlas._to565((bb[:, None] * ax - ab[:, None] * bx) / det[:, None])
    n1 = lod_atlas._to565((aa[:, None] * bx - ab[:, None] * ax) / det[:, None])
    hi, lo = np.maximum(n0, n1), np.minimum(n0, n1)
    ok &= hi > lo
    pal = lod_atlas._palette(hi, lo, np.ones(len(hi), bool)).astype(np.float32)
    nidx = ((rgb[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1).argmin(-1)
    new = ((rgb - np.take_along_axis(pal, nidx[:, :, None], 1)) ** 2).sum((1, 2))
    opal = lod_atlas._palette(c0, c1, np.ones(len(c0), bool)).astype(np.float32)
    old = ((rgb - np.take_along_axis(opal, idx[:, :, None].astype(np.int64), 1)) ** 2).sum((1, 2))
    better = ok & (new < old)
    out = blk.copy()
    out[better] = st._pack(hi[better], lo[better], nidx[better])
    return out


def part(assets, family):
    e = assets.candidates(f'dds/nebula_{family}_background_part_01_diff.pck')[-1]
    return lod_atlas.decode_dds(assets.read_entry(e))[..., :3]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', type=Path, required=True)
    a = ap.parse_args(argv)
    assets = st.source_view(a.game.resolve())
    print('(1) PCA encoder vs one refinement iteration, 2x upsampled stock part 01 (4096x2048):')
    for fam in ('bluewell', 'avdustysol'):
        src = part(assets, fam)
        up = np.clip(st.upsample2(src.astype(np.float32) / 255), 0, 1) * 255
        zero = np.repeat(np.repeat(~src.any(-1), 2, 0), 2, 1)
        up[zero] = 0
        blocks = st._blocks(up.astype(np.float32))
        keep = ~st._blocks(zero[..., None])[:, :, 0].any(1)
        rgb = blocks[keep]
        for label in ('pca', 'pca+refine'):
            t0 = time.perf_counter()
            enc = np.concatenate([lod_atlas._encode_color(rgb[s:s + 32768]) for s in range(0, len(rgb), 32768)])
            if label == 'pca+refine':
                enc = np.concatenate([refine(rgb[s:s + 32768], enc[s:s + 32768]) for s in range(0, len(rgb), 32768)])
            dt = time.perf_counter() - t0
            back = lod_atlas._decode_color(enc, True)[0].astype(np.float32)
            print(f'  {fam} {label}: {dt:.2f} s for {len(rgb)} blocks, encoder PSNR'
                  f' {st.psnr_of(float(((back - rgb) ** 2).mean()))} dB')
    import test_sky_textures as tst
    ideal = np.clip(np.rint(tst.smooth_luma(1024, 512)[..., None] * tst.COLOUR), 0, 255).astype(np.uint8)
    print(f'(2) exact smooth ramp (0.15 level/px), 8-bit: mean identical-luma run {st.mean_run_x(ideal):.2f} px,'
          f' plateau share {st.plateau_share(ideal):.3f} (limits 1.5 px, 0.10)')
    src = part(assets, 'bluewell')[256:768, 512:1536].copy()
    opts = st.options_of()
    up = np.clip(st.upsample2(st.deband(src.astype(np.float32) / 255, src, opts)), 0, 1) * 255
    zero = np.repeat(np.repeat(~src.any(-1), 2, 0), 2, 1)
    up[zero] = 0
    h, w = up.shape[:2]
    fine = st.bayer_offsets(h, w, 1.0)
    coarse = np.repeat(np.repeat(st.bayer_offsets(h // 2, w // 2, 1.0), 2, 0), 2, 1)
    steps = np.array([255 / 31, 255 / 63, 255 / 31], np.float32)
    hdr = lod_atlas.dds_header(w, h, 1, 'A8R8G8B8')
    sdds = lod_atlas.dds_header(w // 2, h // 2, 1, 'A8R8G8B8') + np.concatenate(
        [src[..., ::-1], np.full(src.shape[:2] + (1,), 255, np.uint8)], -1).tobytes()
    print('(3) per-texel RGB565 + ordered dither on the debanded 2x bluewell part 01 crop (looser than DXT1):')
    for label, bay in (('bake-res cells', fine), ('source-res cells', coarse)):
        for amp in (1.0, 2.0):
            q = np.clip(np.rint(up / steps + bay[..., None] * amp) * steps, 0, 255)
            q[zero] = 0
            img = np.concatenate([np.rint(q).astype(np.uint8)[..., ::-1], np.full((h, w, 1), 255, np.uint8)], -1)
            m = st.metrics(sdds, hdr + img.tobytes())
            print(f'  {label}, +-{amp / 2:g} step: run {m["run"]:.2f} px, hue {m["hue_shift_deg"]:.2f} deg,'
                  f' plateau {m["plateau_share"]:.3f}, PSNR {m["psnr"]} dB')
    return 0


if __name__ == '__main__':
    sys.exit(main())
