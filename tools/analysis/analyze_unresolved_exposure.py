#!/usr/bin/env python3
"""Compare exposure policies on recovered *unresolved* HDR readbacks, offline.

These files are not post-TAA meter inputs or presented images. This command
does not reconstruct history, adaptation, deadband or a hypothetical game run.
It calls the reviewed space-aware reference and records its source hashes.
Raw readbacks and the recovery manifest stay outside the repository.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import struct


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(65536), b''):
            digest.update(block)
    return digest.hexdigest()


def require_basename(name: str) -> None:
    if not name or name in ('.', '..') or '/' in name or '\\' in name:
        raise ValueError('snapshot filenames must be basenames')


def validate_entry(entry: dict) -> None:
    """Reject unsuccessful or incompatible captures before reading any pixels."""
    record = entry['log_record']
    require_basename(entry['file'])
    if record['file'] != entry['file']:
        raise ValueError('recovered and logged filenames differ')
    if int(record['result'], 16) != 0 or record['format'] != 'rgba16f_row_major':
        raise ValueError('expected a successful rgba16f_row_major readback')
    width, height = int(record['width']), int(record['height'])
    if width <= 0 or height <= 0:
        raise ValueError('readback dimensions must be positive')
    if int(record['bytes']) != width * height * 8 or entry['bytes'] != width * height * 8:
        raise ValueError('recorded readback sizes disagree with dimensions')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--reference-dir', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    recipe_paths = {name: args.reference_dir / name for name in ('exposure_reference.py', 'agx_reference.py')}
    recipe_paths['analyze_unresolved_exposure.py'] = Path(__file__).resolve()
    recipe_paths['recovered-hdr-inputs.json'] = args.snapshot / 'recovered-hdr-inputs.json'
    # Capture the recipe before importing/executing it, and reject source or
    # manifest changes during the calculation before emitting a result.
    recipe_hashes = {name: sha256(path) for name, path in recipe_paths.items()}
    reference_path = args.reference_dir / 'exposure_reference.py'
    spec = importlib.util.spec_from_file_location('unresolved_exposure_reference', reference_path)
    reference = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reference)
    if not hasattr(reference, 'exposure_target'):
        parser.error('--reference-dir must contain the reviewed space-aware reference')
    manifest = json.loads((args.snapshot / 'recovered-hdr-inputs.json').read_text())
    require_basename(manifest['log'])
    for entry in manifest['files']:
        validate_entry(entry)
    frames = {int(entry['log_record']['frame']) for entry in manifest['files']}
    records = {}
    # Stream the session; retain only the seven relevant per-frame summaries.
    with (args.snapshot / manifest['log']).open() as stream:
        for line in stream:
            kind = line.split(' ', 1)[0]
            if kind not in ('hdr_frame', 'motion_output_frame'):
                continue
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            key = (fields.get('device'), int(fields.get('frame', -1)))
            if key[1] in frames:
                records.setdefault(key, {})[kind] = fields
    results = []
    for entry in manifest['files']:
        record = entry['log_record']
        width, height = int(record['width']), int(record['height'])
        path = args.snapshot / entry['file']
        if path.stat().st_size != width * height * 8 or sha256(path) != entry['sha256']:
            raise ValueError(f'{path.name}: recovery size/hash mismatch')
        context = records[(record['device'], int(record['frame']))]
        hdr, motion = context['hdr_frame'], context['motion_output_frame']
        if hdr['decode'] != 'gamma2.2' or hdr['exposure'] != 'manual' or float(hdr['ev']) != 0:
            raise ValueError('expected the fixed-EV0 gamma2.2 baseline')
        if motion['taa_resolved'] != '1' or motion['taa_hdr'] != '1':
            raise ValueError('expected a resolved HDR frame; reassess input semantics')
        logs = []
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(65536), b''):
                for rgba in struct.iter_unpack('<4e', block):
                    if not all(math.isfinite(c) for c in rgba):
                        raise ValueError(f'{path.name}: nonfinite pixel; define its policy first')
                    logs.append(reference.meter_level0(rgba[:3], hdr['decode']))
        means, maxes, tw, th = reference.reduce_tiles(logs, width, height)
        stats = reference.meter_statistics(means, maxes, weights=reference.tile_weights(tw, th))
        target = reference.exposure_target(stats)
        # The old chain continues to 1x1 with clamped edges. At 1280x768,
        # its result differs from the unweighted mean of the new 80x48 tiles.
        old_log = reference.reduce_chain(means, tw, th)
        old_key = math.log2(reference.KEY) - old_log
        results.append({
            'frame': int(record['frame']), 'file': path.name, 'sha256': entry['sha256'],
            'width': width, 'height': height, 'tile_width': tw, 'tile_height': th,
            'actual_meter_input_available': False,
            'recorded_exposure_mode': 'manual', 'recorded_ev': 0.0,
            'statistics': stats,
            'new_policy_fresh_target': target,
            'new_policy_clamp_active': min(target['ev_key'], target['ev_limit']) != target['ev_target'],
            'old_policy_1x1_log_l': old_log,
            'old_policy_key_ev': old_key,
            'old_policy_target_ev': min(8.0, max(-8.0, old_key)),
        })
    report = {
        'kind': 'offline_unresolved_input_counterfactual',
        'limitation': 'Actual meter consumes post-TAA color, which was not captured. No adaptation or presented-image reconstruction.',
        'recovery_basis': manifest['basis'], 'session_log': manifest['log'],
        'reference_sha256': {name: recipe_hashes[name] for name in ('exposure_reference.py', 'agx_reference.py')},
        'analyzer_sha256': recipe_hashes['analyze_unresolved_exposure.py'],
        'recovery_manifest_sha256': recipe_hashes['recovered-hdr-inputs.json'],
        'policy': {'decode': 'gamma2.2', 'old_ev_range': [-8, 8], 'new_ev_range': [reference.EV_MIN, reference.EV_MAX],
                   'key': reference.KEY, 'meter_floor': reference.METER_FLOOR, 'meter_clip': reference.METER_CLIP,
                   'meter_bg': reference.METER_BG, 'meter_min_lit': reference.METER_MIN_LIT,
                   'white_target': reference.WHITE_TARGET, 'key_pull': reference.KEY_PULL,
                   'edge_weight': reference.EDGE_WEIGHT},
        'frames': results,
    }
    for name, path in recipe_paths.items():
        if sha256(path) != recipe_hashes[name]:
            raise ValueError(f'{name}: recipe changed during calculation; rerun on a stable snapshot')
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    for frame in results:
        target = frame['new_policy_fresh_target']
        print(f"frame={frame['frame']} lit={frame['statistics']['lit_fraction']:.4f} old_ev={frame['old_policy_target_ev']:.4f} "
              f"new_key={target['ev_key']:.4f} limit={target['ev_limit']:.4f} fresh_ev={target['ev_target']:.4f}")


if __name__ == '__main__':
    main()
