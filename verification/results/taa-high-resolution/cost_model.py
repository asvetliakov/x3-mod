#!/usr/bin/env python3
"""Cost model of the TAA sub-passes at 1920x1080 and 5120x1440 (docs/architecture/taa-high-resolution.md section 1).

Inputs: the Run 280 medians (docs/verification/gpu-sync-timing.md "Run 280", each including a 0.264 ms sync-pair
floor), the mask bench (verification/results/taa-stage-cost/mask-cut/bench_out.txt: fixed cost per draw 0.035-0.04 ms,
"every full-screen 16-byte read about 0.1 ms per 2 Mpx" = 330 MB/ms) and the unique bytes each pass touches per pixel,
read from the shaders. Everything printed is inferred arithmetic on those measured inputs.
"""
FLOOR = 0.264            # ms per sync pair, measured (gpu-sync-timing.md, light pair)
DRAW_FIXED = 0.04        # ms per full-screen draw, measured on the bench (constant-output program)
MB_PER_MS = 330.0        # 33 MB per 0.1 ms, measured on the bench
PX_1080 = 1920 * 1080
PX_5K = 5120 * 1440
SCALE = PX_5K / PX_1080  # 3.556
SUBLINEAR = 1138 / 414   # measured D3D9 dilation chain 1080p -> 5120x1440 (d3d11-post-chain-feasibility.md section 3)

# Run 280 medians incl. floor: (copy, mask, box, resolve)
runs = {
    'busy still':   (0.45, 2.13, 1.18, 2.08),
    'busy fast':    (0.46, 2.05, 1.17, 2.50),
    'fogged still': (0.45, 1.23, 0.83, 1.27),
    'fogged moving':(0.45, 1.33, 0.88, 1.37),
}
# unique bytes per pixel (read + write) per sub-pass, from the shaders (busy: hull-heavy reads)
bytes_px = {
    'copy':    (16 + 4),                       # lane read, R32F write
    'mask':    (4 + 16 + 16 + 8 + 4) + 8 + 8,   # tests (depth, motion, lane, scene centre, write) + x (4+4) + y (4+4)
    'box':     (8 + 4 + 16) + (4 + 16 + 4 + 16),# rows (colour, mask, MRT write) + columns (mask, rows, depth, MRT write)
    'resolve': 8 + 4 + 8 + 4 + 16 + 4 + 4 + 16 + 8 + 4,  # colour, depth, prev colour, prev depth, motion, age, mask, box, writes
}
draws = {'copy': 1, 'mask': 3, 'box': 2, 'resolve': 1}
names = ('copy', 'mask', 'box', 'resolve')
print(f"pixels: 1080p {PX_1080/1e6:.2f} Mpx, 5120x1440 {PX_5K/1e6:.2f} Mpx, ratio {SCALE:.3f}, measured dilation-chain ratio {SUBLINEAR:.2f}")
print("\nbandwidth floor per sub-pass at 1080p (unique bytes only):")
total_b = 0
for n in names:
    mb = bytes_px[n] * PX_1080 / 1e6
    total_b += mb
    print(f"  {n:8s} {bytes_px[n]:4d} B/px  {mb:6.1f} MB  {mb / MB_PER_MS:5.2f} ms  (+{draws[n]} draw fixed {draws[n]*DRAW_FIXED:.2f} ms)")
print(f"  total    {sum(bytes_px.values()):4d} B/px  {total_b:6.1f} MB  {total_b / MB_PER_MS:5.2f} ms; at 5120x1440 {total_b*SCALE:6.0f} MB  {total_b*SCALE/MB_PER_MS:5.2f} ms")
print("\nnet of floors (measured medians minus 0.264 per sub-pass) and the 5120x1440 projection (fixed draw cost held, the rest x3.556; lower bound x2.75):")
for label, row in runs.items():
    net = [v - FLOOR for v in row]
    total = sum(net)
    fixed = sum(draws[n] * DRAW_FIXED for n in names)
    hi = (total - fixed) * SCALE + fixed
    lo = total * SUBLINEAR
    parts = ' '.join(f"{n}={v:.2f}" for n, v in zip(names, net))
    print(f"  {label:14s} {parts}  total={total:.2f} ms  ->  5120x1440 {lo:.1f}-{hi:.1f} ms")
print("\nbandwidth share of the busy-still net total:", f"{total_b / MB_PER_MS / sum(v - FLOOR for v in runs['busy still']):.0%}")
