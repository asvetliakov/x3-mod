#!/usr/bin/env python3
"""How the HDR meter's highlight guard reacts to the own sun's disc (docs/architecture/own-sun-pass.md section 2; the meter
is src/renderer/exposure.h, unchanged). Offline arithmetic, no device:
  tiles     the meter's reduce chain: 4x per axis at least once, then until neither axis exceeds 128 (tile = 4^steps px)
  disc      tiles holding a pixel centre inside the core r <= 0.9 r_disc (D = 1: full radiance), centred disc, worst and
            best of 16 sub-tile placements
  p99       ascending tile maxima at index tiles * 99 / 100: the disc sets it once the disc tiles reach the top 1 %
  ev_limit  log2(0.9 * 16.2917424) - log2(min(L, meter_clip 64)), L the decoded luminance of a white disc of radiance K:
            K^2.2 under the default X3M_HDR_DECODE=gamma2.2, K under decode=identity
The Auto cap is +1.3 EV; ev_limit below it caps the exposure while the disc holds p99.
Usage: python3 meter_exposure.py  (prints one row per resolution and radius definition, then the radiance thresholds)
"""
import math

WHITE = 0.9 * 16.2917424
CLIP = 64.0


def tile_grid(width, height):
    w, h, steps = width, height, 0
    while True:
        w, h, steps = math.ceil(w / 4), math.ceil(h / 4), steps + 1
        if w <= 128 and h <= 128:
            return w, h, 4 ** steps


def disc_tiles(width, height, tile, radius, ox, oy):
    cx, cy, core = width / 2 + ox, height / 2 + oy, 0.9 * radius
    hit = set()
    for y in range(int(cy - core) - 1, int(cy + core) + 2):
        for x in range(int(cx - core) - 1, int(cx + core) + 2):
            if (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= core * core:
                hit.add((x // tile, y // tile))
    return len(hit)


def ev_limit(luminance):
    return math.log2(WHITE) - math.log2(min(luminance, CLIP))


def main():
    for width, height in ((1280, 768), (1920, 1080), (5120, 1440)):
        tw, th, tile = tile_grid(width, height)
        tiles = tw * th
        need = tiles - tiles * 99 // 100
        for name, radius in (('width 0.04', 0.04 * width), ('height 0.0667', 0.0667 * height)):
            counts = [disc_tiles(width, height, tile, radius, tile * i / 4, tile * j / 4) for i in range(4) for j in range(4)]
            print('%dx%d tiles %dx%d (%d px) radius %s = %.1f px: disc tiles %d..%d of %d = %.2f..%.2f %%, p99 needs %d -> %s'
                  % (width, height, tw, th, tile, name, radius, min(counts), max(counts), tiles, 100 * min(counts) / tiles,
                     100 * max(counts) / tiles, need, 'disc sets p99' if min(counts) >= need else
                     'disc sets p99 at some placements' if max(counts) >= need else 'below p99'))
    for k in (8.0, 4.0, 3.39, 2.25):
        print('K %.2f: gamma2.2 L %.1f ev_limit %+.2f EV; identity L %.2f ev_limit %+.2f EV'
              % (k, k ** 2.2, ev_limit(k ** 2.2), k, ev_limit(k)))
    print('ev_limit >= 0 needs K <= %.2f, >= +1.3 (no effect under the Auto cap) K <= %.2f (gamma2.2)'
          % (WHITE ** (1 / 2.2), (WHITE / 2 ** 1.3) ** (1 / 2.2)))


if __name__ == '__main__':
    main()
