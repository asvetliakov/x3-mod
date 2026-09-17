#!/usr/bin/env python3
"""Re-roll the sun-shadow receiver depth by one fp32 ULP and count the pixels
whose shadow factor flips (docs/verification/directional-shadows.md, "Run 40 A
(run117) diagnosis" section 2; docs/architecture/shadow-receiver-depth.md
section 4, the captured-data witness).

RT2 stores the receiver as device depth d = z/w in fp32 and the apply
reconstructs z = m32 / (d - m22): one ULP of d near 1 is a view-depth step of
z^2 / 1e8 world units (13.6 u at 37 km, 85 u at 92 km), which on the far
cascades moves the receiver by a fraction of a map texel and re-rolls the 3x3
compare every frame. The proposed encoding stores linear view depth w in fp32,
whose ULP is 6e-8 z (2e-3 u at 37 km).

This tool measures both on captured data. For one F8 frame it loads the RT2
dump (`depth_<device>_<frame>.rg32f`, G32R32F, .r = z/w, .g = share), the
cascade maps (`shadow_map<k>_<device>_<frame>.r32f`, R32F) and the frame's
`sun_shadow_apply_params` line out of the session log (streamed, never loaded
whole), runs `expected_factor_cascades` (verification/probe/sun_shadow_apply.py)
on the stored receiver and on the receiver moved by +-1 ULP of each encoding,
and reports per cascade, over the pixels that cascade owns:

  changed            the cascade's own 3x3 f differs at all under either sign
  flipped            |df| >= threshold (default 2/9, one of the nine taps), on
                     that own f; `flipped_blended` is the same count on the
                     blended factor that reaches the screen
  margin p25/50/75   (map depth at the receiver's nearest texel - receiver
                     sun depth) * depth range, world units, on the flipped
                     class: the diagnosis' witness that the map holds the
                     receiver's own single-sided surface
  receiver step      median |dz| of one ULP in world units

Baselines, run117 device 1, coarse derivatives, threshold 2/9: 9.1 % of the
90.8 k C3-owned pixels at frame 24624, 14.2 % of the 26.5 k C3-owned and 15.7 %
of the 3.0 k C4-owned at 14780. The design predicts < 0.05 % under the w
encoding.
"""
import argparse
import json
import sys
from pathlib import Path

PARAMS_PREFIX = 'sun_shadow_apply_params '
DEFAULT_THRESHOLD = 2.0 / 9.0
ENCODINGS = ('zw', 'w')


class MalformedInput(Exception):
    """A telemetry line or a dump file that does not match the contract."""


def _twin():
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'verification/probe'))
    import sun_shadow_apply
    return sun_shadow_apply


def read_apply_params(log, frames, device=1):
    """Stream `log` once and return {frame: (params, extra)} of the
    `sun_shadow_apply_params` lines of `frames` on `device`."""
    twin = _twin()
    wanted, found = set(frames), {}
    with open(log, 'r', errors='replace') as stream:
        for line in stream:
            if not line.startswith(PARAMS_PREFIX):
                continue
            fields = twin.line_fields(line)
            if int(fields.get('device', -1)) != device:
                continue
            frame = int(fields.get('frame', -1))
            if frame not in wanted or frame in found:
                continue
            params, extra = twin.parse_apply_params(fields)
            if 'cascades' not in params:
                raise MalformedInput('frame %d: single-map apply line, not a cascade one' % frame)
            found[frame] = (params, extra)
            if len(found) == len(wanted):
                break
    missing = sorted(wanted - set(found))
    if missing:
        raise MalformedInput('no sun_shadow_apply_params line for device %d frame(s) %s'
                             % (device, ', '.join(str(f) for f in missing)))
    return found


def load_frame(directory, device, frame, params, extra):
    """The frame's RT2 (d, s) and its cascade maps (None where absent)."""
    twin = _twin()
    width, height = extra['width'], extra['height']
    path = Path(directory) / ('depth_%d_%d.rg32f' % (device, frame))
    if not path.is_file():
        raise MalformedInput('no RT2 dump %s' % path)
    data = path.read_bytes()
    if len(data) != width * height * 8:
        raise MalformedInput('%s: %d bytes != %d x %d x 8 (G32R32F)' % (path.name, len(data), width, height))
    d, s = twin.unpack_rt2(data, width, height)
    return d, s, twin.load_cascade_maps(directory, device, frame, extra, params)


def perturbed_depth(d, params, encoding):
    """(baseline, plus, minus) stored-depth arrays for one receiver encoding.

    Every array is the `d` the apply would read back, in float64, such that the
    twin's z = m32 / (d - m22) is the encoded receiver depth. `zw`: the RT2
    value itself, quantized to fp32 and moved by +-1 ULP. `w`: linear view
    depth quantized to fp32 and moved by +-1 ULP, re-expressed as a d (the
    round trip is exact to ~1e-12 of the value, four orders below an fp32 w
    ULP). Pixels without a finite positive view depth (the sentinel d < 0,
    d == m22) keep their stored value in all three arrays; they are never
    valid receivers.
    """
    import numpy as np
    if encoding not in ENCODINGS:
        raise MalformedInput('unknown encoding %r' % (encoding,))
    m22, m32 = params['m22'], params['m32']
    stored = np.float32(d).astype(np.float64)
    if encoding == 'zw':
        base32 = np.float32(d)
        return (base32.astype(np.float64),
                np.nextafter(base32, np.float32(np.inf)).astype(np.float64),
                np.nextafter(base32, np.float32(-np.inf)).astype(np.float64))
    with np.errstate(divide='ignore', invalid='ignore'):
        z = m32 / (stored - m22)
    usable = np.isfinite(z) & (z > 0.0) & (stored >= 0.0)
    w32 = np.float32(np.where(usable, z, 1.0))
    out = []
    for target in (w32, np.nextafter(w32, np.float32(np.inf)), np.nextafter(w32, np.float32(-np.inf))):
        with np.errstate(divide='ignore', invalid='ignore'):
            back = m32 / target.astype(np.float64) + m22
        out.append(np.where(usable, back, stored))
    return tuple(out)


def _nearest_map_depth(sun_position, sun_map):
    """The map depth at the texel holding the receiver (the twin's non-legacy
    rule: round(muv * N)), and whether that texel is occupied."""
    import numpy as np
    size = sun_map.shape[0]
    mu = sun_position[0] * .5 + .5
    mv = .5 - sun_position[1] * .5
    tu = np.clip(np.floor(np.nan_to_num(mu) * size + .5).astype(np.int64), 0, size - 1)
    tv = np.clip(np.floor(np.nan_to_num(mv) * size + .5).astype(np.int64), 0, size - 1)
    return sun_map[tv, tu]


def reroll_frame(d, s, maps, params, extra, cascades=None, threshold=DEFAULT_THRESHOLD, coarse=True, frame=None):
    """The +-1 ULP experiment on one frame, per cascade. Returns a record."""
    import numpy as np
    twin = _twin()
    count = len(params['cascades'])
    report = [] if cascades is None else sorted(set(cascades))
    for c in report:
        if not 0 <= c < count:
            raise MalformedInput('cascade %d outside the frame\'s %d slots' % (c, count))
    factors, steps = {}, {}
    for encoding in ENCODINGS:
        base, plus, minus = perturbed_depth(d, params, encoding)
        factors[encoding] = [twin.expected_factor_cascades(value, s, maps, params, coarse=coarse)
                             for value in (base, plus, minus)]
        with np.errstate(divide='ignore', invalid='ignore'):
            steps[encoding] = np.abs(params['m32'] / (plus - params['m22']) - params['m32'] / (base - params['m22']))
    stored = factors['zw'][0]
    valid, selected = stored['valid'], stored['selected']
    if report == []:
        report = [c for c in range(count) if bool((valid & (selected == c)).any())]
    record = {'frame': frame, 'threshold': threshold, 'coarse': bool(coarse),
              'valid': int(valid.sum()), 'cascades': []}
    for c in report:
        owned = valid & (selected == c)
        owned_count = int(owned.sum())
        entry = {'cascade': c, 'owned': owned_count,
                 'source': extra['cascades'][c]['source'] if c < len(extra['cascades']) else c,
                 'encodings': {}}
        sun_map = maps[c]
        depth_range = None
        if c < len(extra['cascades']):
            detail = extra['cascades'][c]
            depth_range = detail['depth_light'] + detail['depth_behind']
        for encoding in ENCODINGS:
            base_run, plus_run, minus_run = factors[encoding]
            # The owning cascade's own 3x3 result is the compare under test (the
            # diagnosis' figures); the blended f is what reaches the screen and
            # differs only inside the blend band, where the next cascade dilutes it.
            own = lambda run: run['per_cascade'][c]
            delta = np.maximum(np.abs(own(plus_run) - own(base_run)), np.abs(own(minus_run) - own(base_run)))
            blended = np.maximum(np.abs(plus_run['f'] - base_run['f']), np.abs(minus_run['f'] - base_run['f']))
            changed = owned & (delta > 0.0)
            flipped = owned & (delta >= threshold - 1e-12)
            flipped_count = int(flipped.sum())
            blended_count = int((owned & (blended >= threshold - 1e-12)).sum())
            step = steps[encoding]
            part = {'changed': int(changed.sum()),
                    'changed_fraction': (int(changed.sum()) / owned_count) if owned_count else 0.0,
                    'flipped': flipped_count,
                    'flipped_fraction': (flipped_count / owned_count) if owned_count else 0.0,
                    'flipped_blended': blended_count,
                    'flipped_blended_fraction': (blended_count / owned_count) if owned_count else 0.0,
                    'receiver_step_units_p50': float(np.median(step[owned])) if owned_count else None,
                    'margin_units_p25_p50_p75': None}
            if flipped_count and sun_map is not None and depth_range:
                margin = (_nearest_map_depth(base_run['sun'][c], sun_map) - base_run['sun'][c][2]) * depth_range
                part['margin_units_p25_p50_p75'] = [float(v) for v in np.percentile(margin[flipped], [25, 50, 75])]
            entry['encodings'][encoding] = part
        record['cascades'].append(entry)
    return record


def reroll(directory, log, frames, cascades=None, device=1, threshold=DEFAULT_THRESHOLD, coarse=True):
    """`reroll_frame` over a list of capture frames of one run directory."""
    lines = read_apply_params(log, frames, device)
    records = []
    for frame in sorted(set(frames)):
        params, extra = lines[frame]
        d, s, maps = load_frame(directory, device, frame, params, extra)
        records.append(reroll_frame(d, s, maps, params, extra, cascades, threshold, coarse, frame))
        del d, s, maps
    return records


def format_record(record):
    lines = ['frame %s  valid %d  threshold %.4f  derivatives %s'
             % (record['frame'], record['valid'], record['threshold'], 'coarse' if record['coarse'] else 'fine')]
    for entry in record['cascades']:
        lines.append('  c%d owned %d' % (entry['cascade'], entry['owned']))
        for encoding in ENCODINGS:
            part = entry['encodings'][encoding]
            margin = part['margin_units_p25_p50_p75']
            lines.append('    %-3s ULP step %s u  changed %d (%.2f %%)  flipped %d (%.2f %%, blended %.2f %%)  margin p25/50/75 %s'
                         % (encoding,
                            ('%.3g' % part['receiver_step_units_p50']) if part['receiver_step_units_p50'] is not None else '-',
                            part['changed'], 100.0 * part['changed_fraction'],
                            part['flipped'], 100.0 * part['flipped_fraction'],
                            100.0 * part['flipped_blended_fraction'],
                            ('%+.0f / %+.0f / %+.0f' % tuple(margin)) if margin else '-'))
    return '\n'.join(lines)


def parse_frames(text):
    frames = []
    for piece in text.replace(',', ' ').split():
        if '-' in piece[1:]:
            first, _, last = piece.partition('-')
            frames.extend(range(int(first), int(last) + 1))
        else:
            frames.append(int(piece))
    return sorted(set(frames))


def find_log(directory):
    candidates = sorted(Path(directory).glob('session-*.log'))
    if len(candidates) != 1:
        raise MalformedInput('%d session-*.log in %s; pass --log' % (len(candidates), directory))
    return candidates[0]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--run', required=True, help='run directory with the F8 depth_/shadow_map dumps')
    parser.add_argument('--log', help='session log (default: the single session-*.log of --run)')
    parser.add_argument('--frames', required=True, help='capture frames, e.g. 24624 or 14780,24624')
    parser.add_argument('--cascade', type=int, action='append', dest='cascades',
                        help='cascade slot to report, repeatable (default: every slot that owns a pixel)')
    parser.add_argument('--device', type=int, default=1)
    parser.add_argument('--threshold', type=float, default=DEFAULT_THRESHOLD,
                        help='|df| counted as a flip (default 2/9, one tap of the 3x3)')
    parser.add_argument('--fine', action='store_true',
                        help='fine quad derivatives (default: the coarse ones the diagnosis used)')
    parser.add_argument('--json', help='write the records as JSON here ("-" for stdout)')
    args = parser.parse_args(argv)
    log = args.log or find_log(args.run)
    records = reroll(args.run, log, parse_frames(args.frames), args.cascades, args.device,
                     args.threshold, not args.fine)
    if args.json == '-':
        json.dump(records, sys.stdout, indent=1, sort_keys=True)
        sys.stdout.write('\n')
    else:
        for record in records:
            print(format_record(record))
        if args.json:
            Path(args.json).write_text(json.dumps(records, indent=1, sort_keys=True) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
