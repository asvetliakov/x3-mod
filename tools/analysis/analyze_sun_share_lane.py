#!/usr/bin/env python3
"""Diagnostic eligibility at the recorded pre-AO/TAA scene-end publication.

No shadow application. A raw nonnegative .g is never sufficient: unavailable
frames and absent/failed same-frame coverage produce zero eligible pixels.
"""
import argparse
import json
import math
from pathlib import Path
import struct


REASONS = ('unknown', 'feature', 'scene', 'unregistered', 'pair', 'no_zwrite', 'blended', 'state', 'rows',
           'geometry', 'no_depth', 'fade_arm', 'apply_failed', 'scope', 'history', 'read_failed')
WRITER_FIELDS = ('vs', 'ps', 'reason', 'gate', 'registered', 'z', 'zwrite', 'z_known', 'declaration', 'stride')


def refusals(fields):
    """Bucket counts of one sun_shadow_lane_refusals line, checked against its untracked total."""
    if any(name not in fields for name in REASONS) or 'untracked' not in fields:
        raise ValueError('refusal_line_truncated')
    buckets = {name: int(fields[name]) for name in REASONS}
    total = int(fields['untracked'])
    if sum(buckets.values()) != total:
        raise ValueError('refusal_buckets_mismatch')
    return dict(untracked=total, buckets=buckets, signatures=int(fields.get('signatures', 0)),
                overflow=int(fields.get('overflow', 0)))


def analyze(log, directory):
    frames = {}
    writers = []
    tags = {'sun_shadow_lane_frame': 'publication', 'motion_output_depth_readback': 'depth',
            'sun_shadow_lane_coverage_readback': 'coverage', 'sun_shadow_lane_refusals': 'refusals',
            'sun_shadow_lane_writer': 'writer'}
    with Path(log).open(errors='replace') as stream:
        for line in stream:
            parts = line.split()
            index = next((i for i, part in enumerate(parts) if part in tags), None)
            if index is None:
                continue
            fields = dict(part.split('=', 1) for part in parts[index+1:] if '=' in part)
            if tags[parts[index]] == 'writer':
                if all(name in fields for name in WRITER_FIELDS) and 'device' in fields and 'frame' in fields:
                    writers.append(dict(device=int(fields['device']), frame=int(fields['frame']),
                                        **{name: fields[name] if name in ('vs', 'ps', 'declaration', 'reason') else int(fields[name])
                                           for name in WRITER_FIELDS}))
                continue
            if 'device' in fields and 'frame' in fields:
                frames.setdefault((fields['device'], fields['frame']), {})[tags[parts[index]]] = fields
    output = []
    for (device, frame), records in sorted(frames.items(), key=lambda item: tuple(map(int, item[0]))):
        publication = records.get('publication')
        if not publication:
            continue
        row = dict(device=int(device), frame=int(frame), available=False, eligible_pixels=0,
                   receiver_draws=int(publication.get('receiver_draws', 0)),
                   untracked_writers=int(publication.get('untracked_writers', 0)),
                   # Grammar with the non-writer counter (only depth writers veto); an
                   # older build's line is flagged rather than read as zero.
                   non_depth_writers=int(publication['non_depth_writers']) if 'non_depth_writers' in publication else None,
                   grammar_old='non_depth_writers' not in publication, reason='frame_unavailable',
                   diagnostics_malformed=None)
        output.append(row)
        # Diagnostics are attached beside the substantive analysis and never
        # replace it: a malformed bucket line flags the frame and the
        # readback/eligibility rows below are still produced.
        try:
            refused = records.get('refusals')
            if refused:
                refused = refusals(refused)
                if refused['untracked'] != row['untracked_writers']:
                    raise ValueError('refusal_total_mismatch')
                row.update(untracked_reasons=refused['buckets'], writer_signatures=refused['signatures'],
                           writer_overflow=refused['overflow'])
            elif row['untracked_writers']:
                row['untracked_reasons'] = None  # older build without the refusals line
        except (ValueError, KeyError) as error:
            row['diagnostics_malformed'] = str(error)
        if publication.get('available') != '1':
            continue
        try:
            depth = records.get('depth', {})
            if depth.get('result') != '00000000' or depth.get('format') != 'rg32f_row_major':
                raise ValueError('enhanced_depth_unavailable')
            width, height = int(depth['width']), int(depth['height'])
            pixels = width*height
            data = (Path(directory)/depth['file']).read_bytes()
            if len(data) != pixels*8:
                raise ValueError('depth_size')
            mask = [0.0]*pixels
            if publication.get('exclusion_required') == '1':
                coverage = records.get('coverage', {})
                if publication.get('exclusion_valid') != '1' or coverage.get('result') != '00000000':
                    raise ValueError('coverage_unavailable')
                if coverage.get('format') != 'rgba16f_row_major' or int(coverage['width']) != width or int(coverage['height']) != height:
                    raise ValueError('coverage_shape')
                packed = (Path(directory)/coverage['file']).read_bytes()
                if len(packed) != pixels*8:
                    raise ValueError('coverage_size')
                mask = [values[0] for values in struct.iter_unpack('<4e', packed)]
                if any(not math.isfinite(value) or value < 0 for value in mask):
                    raise ValueError('coverage_invalid')
            candidates = valid_zero = excluded = invalid = 0
            for (depth_value, share), coverage in zip(struct.iter_unpack('<2f', data), mask):
                if not math.isfinite(depth_value) or not 0 <= depth_value <= 1:
                    continue
                if not math.isfinite(share) or not 0 <= share <= 1:
                    invalid += 1
                    continue
                candidates += 1
                if coverage > 0:
                    excluded += 1
                else:
                    valid_zero += share == 0
            row.update(available=True, reason='qualified_publication', width=width, height=height,
                       eligible_pixels=candidates-excluded, zero_sun_pixels=valid_zero,
                       excluded_pixels=excluded, invalid_share_pixels=invalid)
        except (OSError, ValueError, KeyError, struct.error) as error:
            row['reason'] = str(error)
    totals = {name: sum(row['untracked_reasons'][name] for row in output if row.get('untracked_reasons')) for name in REASONS}
    return dict(frames=output, untracked_reason_totals=totals,
                refusal_frames=sum(1 for row in output if row.get('untracked_reasons')),
                writers=writers, shadow_application=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--readback-dir', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.log, args.readback_dir or args.log.parent), indent=2))


if __name__ == '__main__':
    main()
