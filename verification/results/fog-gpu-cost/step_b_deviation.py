#!/usr/bin/env python3
"""Fog route step B (docs/architecture/fog-gpu-cost.md): how far the 24-far-bin look moves from the accepted 40-bin look,
per look case, from a fixture summary written with --variant-reference and the far24.json beside it: GPU far24 against GPU 40-bin images over all
9,216 pixels of each 128x72 case (production RGBA16F target; T and S absolute, max and mean over all / fogged pixels,
pixels past the .003 gate), the host law against the host law on the reference rays, the repair split, and the
variant's own gates against its host reference.
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_b_deviation.py <output>/summary.json"""
import json
import sys
from pathlib import Path


def main():
    s = json.loads(Path(sys.argv[1]).read_text()); v = json.loads((Path(sys.argv[1]).parent / s['far_bins_variant_file']).read_text())
    print(f"result={s['result']} gates={sum(s['gates'].values())}/{len(s['gates'])} far24_gates=" +
          ','.join(f"{k}={s['gates'][k]}" for k in s['gates'] if k.startswith('far24_')))
    print('case                 | GPU RGBA16F: T max  T mean(fog) S max  S mean(fog) past.003/fogged | host: T max  S max  past.003/rays '
          '| vs own ref: T max S max (bilinear16)')
    for label, d in v['deviation_from_40_bins'].items():
        g, h, own = d['gpu_bilinear16'], d['host'], v['look_versus_host'][label]['bilinear16']
        print(f"{label:20s} | {g['T_max']:.4f} {g['T_mean_fogged']:.5f} {g['S_max']:.4f} {g['S_mean_fogged']:.5f} {g['pixels_past_gate']:5d}/{g['fogged']:5d} "
              f"| {h['T_max']:.4f} {h['S_max']:.4f} {h['pixels_past_gate']:4d}/{h['pixels']:4d} | {own['T']['max']:.5f} {own['S']['max']:.5f}")
    r = v['repair_with_shafts']
    print(f"repair_shafts_far24: fogged={r['fogged']} versus_host_max={r['versus_host']['max']:.5f} other_law_max={r['other_law']['max']:.4f} "
          f"moved_from_40_bins max={r['moved_from_40_bins']['max']:.4f} p99={r['moved_from_40_bins']['p99']:.4f}")
    for name, row in v['programs'].items():
        print(f"{name}: slots={row['slots']} texture_instructions={row['texture_instructions']} loops={row['loops']}")
    print('pass fixture far24 checks:', len(v['pass_fixture_checks']), 'failed:', [k for k, x in v['pass_fixture_checks'].items() if x != 'PASS'])
    far = s['pass_fixture'].get('far24') or {}
    print('pass fixture FAR24 row:', ' '.join(f'{k}={far[k]}' for k in far))
    slope = s.get('fixture_march_slope_timing_not_game_fps', {})
    for key in ('march_sky_look', 'march_sky_look_far24', 'march_depth29300_look', 'march_depth29300_look_far24'):
        if key in slope:
            print(f"fixture slope {key}: per_march_slope_ms={slope[key].get('per_march_slope_ms')} (fixture GPU time, not game FPS)")
    return 0 if s['result'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
