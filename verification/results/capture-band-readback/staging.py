"""Band rows and system-memory staging per capture burst at 5120x1440, before and after the banded readback.

The rule mirrors readback_band_rows() in src/proxy/motion_output.cpp (4 MiB budget, multiple of 16 rows when at
least 16 fit, at most the height). The target list is Run 134 A's capture frame (run134-dxvk-triage/alloc/):
hdr RGBA16F, motion RGBA32F, depth RGBA32F (the sun-shadow lane's 16 B/px RT2: 118 MB in the triage) at 5120x1440
and five shadow cascades (2048, 4096, 4096, 4096, 2048 R32F).
Before: every file allocated a whole SYSTEMMEM surface, per frame. After: one SYSTEMMEM band surface (plus one
DEFAULT render target of the same size) per distinct (format, width, rows), held for the burst.
"""
BUDGET = 4 << 20
TARGETS = [('hdr', 'rgba16f', 5120, 1440, 8), ('motion', 'rgba32f', 5120, 1440, 16), ('depth', 'rgba32f', 5120, 1440, 16)] + \
          [(f'shadow_map{i}', 'r32f', s, s, 4) for i, s in enumerate((2048, 4096, 4096, 4096, 2048))]


def rows(width, bpp, height):
    n = BUDGET // (width * bpp) or 1
    if n >= 16:
        n &= ~15
    return min(n, height)


before = sum(w * h * b for _, _, w, h, b in TARGETS)
pairs = {}
for name, fmt, w, h, b in TARGETS:
    r = rows(w, b, h)
    bands = -(-h // r)
    pairs[(fmt, w, r)] = w * r * b
    print(f'{name:12s} {fmt:8s} {w}x{h} rows={r} bands={bands} band_bytes={w * r * b} whole_bytes={w * h * b}')
print(f'before: {before} bytes per capture frame ({before / 1e6:.1f} MB), {8 * before / 1e9:.2f} GB per 8-frame burst')
print(f'after: {len(pairs)} pairs, {sum(pairs.values())} bytes SYSTEMMEM ({sum(pairs.values()) / 1e6:.1f} MB) for the '
      f'whole burst, the same again in DEFAULT render targets')
