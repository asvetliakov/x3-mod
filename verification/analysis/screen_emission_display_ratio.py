"""Run-17 defect measurement: display luminance of a bullet bolt under the
native screen blend, the withdrawn step C packed law (per-fragment decode),
the display-referred option (a) and the ratified step E law (decode the
accumulated native value once, scale by g), through the run-17 write-back
(gamma-2.2 decode, manual EV 0, AgX, no look, no clamp;
docs/architecture/screen-emission-region.md, steps C and E).

Captured inputs (run 17, draw index 17 of frame 6383): ADD ONE/INVSRCCOLOR, gain
1, 198 primitives per draw (99 quads, a chain of overlapping sprites per
bolt), a DXT5 bullet sprite (identity 2121, not captured). The sprite is a
synthetic soft blob T(d) = tint * exp(-d^2 / 2 sigma^2) (tint the green bolt
(0.55, 1, 0.45)), vertex intensity h = 1, `overlap` copies spaced along the
flight axis. The float64 laws are the runner's (run_linear_distance_fade_live:
screen_law, screen_native); the AgX display encode is tools/analysis/agx_reference.

Prints the per-pixel table and the tail/core ratios; no D3D, no Wine.
"""
import argparse
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import agx_reference as agx  # noqa: E402

TINT = (0.55, 1.0, 0.45)
LUMA = (0.2126, 0.7152, 0.0722)


def encode(x):
    return max(x, 0.0) ** (1 / 2.2) if x > 0 else 0.0


def decode(x):
    return max(x, 1e-10) ** 2.2


def fragments(distance, overlap, sigma, spacing):
    """Sprite values q_i of the `overlap` chain at a pixel `distance` off the
    bolt axis, sampled at the chain's middle (every copy contributes)."""
    values = []
    for i in range(overlap):
        along = (i - (overlap - 1) / 2) * spacing
        g = math.exp(-(distance * distance + along * along) / (2 * sigma * sigma))
        values.append(tuple(t * g for t in TINT))
    return values


def native(background, chain, h=1.0):
    b = list(background)
    for q in chain:
        b = [q[c] * h + (1 - q[c] * h) * b[c] for c in range(3)]
    return tuple(b)


def packed(background, chain, h=1.0, gain=1.0):
    """The withdrawn step C law: L' = E + (1 - q) L with E = decode(T) h gain per fragment."""
    light = [decode(background[c]) for c in range(3)]
    for q in chain:
        light = [decode(q[c]) * h * gain + (1 - q[c] * h) * light[c] for c in range(3)]
    return tuple(encode(x) for x in light)


def option_a(background, chain, h=1.0):
    """Display-referred source: the linear radiance that reproduces the native
    display result under the active write-back is decode(native B), so the
    composed A is native B itself (the packed red lane), exact by construction
    for every order and overlap; it carries no enhancement."""
    return native(background, chain, h)


def step_e(background, chain, h=1.0, gain=1.0):
    """The ratified step E law: accumulate the native encoded value exactly as
    the game does, decode ONCE at publication and scale the bolt's own
    contribution, C = encode(decode(A) + gain (decode(B_native) - decode(A)))."""
    b = native(background, chain, h)
    return tuple(encode(decode(background[c]) + gain * (decode(b[c]) - decode(background[c]))) for c in range(3))


def display(engine_rgb):
    return agx.tonemap_engine(engine_rgb, exposure=1.0, decode_mode='gamma2.2')


def luma(rgb):
    return sum(w * c for w, c in zip(LUMA, rgb))


def measure(background=(0.0, 0.0, 0.0), overlap=8, sigma=3.0, spacing=2.0, gains=(1.0, 2.0, 4.0), distances=(0.0, 3.0, 5.0, 7.0)):
    rows = []
    for distance in distances:
        chain = fragments(distance, overlap, sigma, spacing)
        row = dict(distance=distance, native=luma(display(native(background, chain))),
                   option_a=luma(display(option_a(background, chain))))
        for gain in gains:
            row[f'packed_gain_{gain:g}'] = luma(display(packed(background, chain, gain=gain)))
        for gain in gains:
            row[f'step_e_gain_{gain:g}'] = luma(display(step_e(background, chain, gain=gain)))
        rows.append(row)
    core = rows[0]
    ratios = {}
    for key in core:
        if key == 'distance':
            continue
        ratios[key] = [row[key] / core[key] if core[key] else float('nan') for row in rows]
    return rows, ratios


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--overlap', type=int, default=8, help='sprites overlapping one bolt pixel (99 quads per draw in run 17)')
    parser.add_argument('--sigma', type=float, default=3.0, help='sprite falloff in pixels')
    parser.add_argument('--spacing', type=float, default=2.0, help='sprite spacing along the flight axis in pixels')
    parser.add_argument('--background', type=float, nargs=3, default=(0.0, 0.0, 0.0), help='encoded A behind the bolt (space 0, nebula ~0.2)')
    args = parser.parse_args(argv)
    rows, ratios = measure(tuple(args.background), args.overlap, args.sigma, args.spacing)
    keys = [k for k in rows[0] if k != 'distance']
    print('display luminance (AgX, EV 0, gamma-2.2 decode); overlap=%d sigma=%g spacing=%g background=%s' % (args.overlap, args.sigma, args.spacing, tuple(args.background)))
    print('distance_px ' + ' '.join('%14s' % k for k in keys))
    for row in rows:
        print('%11g ' % row['distance'] + ' '.join('%14.4f' % row[k] for k in keys))
    print('ratio to the core (distance 0):')
    for row_index, row in enumerate(rows):
        print('%11g ' % row['distance'] + ' '.join('%14.3f' % ratios[k][row_index] for k in keys))
    return 0


if __name__ == '__main__':
    sys.exit(main())
