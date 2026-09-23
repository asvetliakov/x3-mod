#!/usr/bin/env python3
"""Fog route step C (docs/architecture/fog-gpu-cost.md): how far the quarter-resolution march moves the image from the
half-resolution one, from the fixture's q4.json (fog_density_shader_run.py check --scale4-reference).
Look cases: the production FP16 march images of both spacings upsampled to the 256x144 screen by the composite's bilinear
law (one depth class per case), fogged pixels only. Depth edges: the edge chain (march, composite, repair into an FP32 target
over scenes 0 and 1: S and the tinted T^k per channel) at both spacings against the full-resolution truth and each other.
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_c_deviation.py [q4.json]"""
import json
import sys
from pathlib import Path

path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'fog-density-shader/q4.json'
q = json.loads(path.read_text())
print('look cases, scale 4 vs scale 2 on the 256x144 screen (fogged pixels):')
print(f"{'case':22s} {'fogged':>6s} {'T max':>7s} {'T mean':>7s} {'S max':>7s} {'S mean':>7s} {'past .003':>10s} {'signed T mean':>13s}")
for case, d in sorted(q['deviation_from_scale_2'].items()):
    print(f"{case:22s} {d['fogged']:6d} {d['T_max']:7.4f} {d['T_mean']:7.4f} {d['S_max']:7.4f} {d['S_mean']:7.4f} {d['past_gate']:5d} {d['past_gate_fraction']:4.1%} {d['signed_T_mean']:+13.5f}")
e = q['depth_edges']
print(f"\ndepth-edge chain {e['width']}x{e['height']}: geometry {e['geometry_pixels']} sky {e['sky_pixels']} edge band {e['edge_band_pixels']} "
      f"(class {e['class_band_pixels']}, geometry depth {e['depth_band_pixels']}); needs_repair twin s2 {e['needs_repair']['2']} s4 {e['needs_repair']['4']}; "
      f"written by the GPU repair s2 {e['repaired_gpu']['2']} s4 {e['repaired_gpu']['4']}")
print(f"{'pixels':26s} {'n':>6s} {'compare':12s} {'T max':>7s} {'T mean':>8s} {'S max':>7s} {'S mean':>8s} {'past .003':>10s}")
for label, rows in e['rows'].items():
    for compare in ('s2_vs_truth', 's4_vs_truth', 's4_vs_s2'):
        r = rows[compare]
        if not r.get('pixels'):
            continue
        print(f"{label:26s} {r['pixels']:6d} {compare:12s} {r['T_max']:7.4f} {r['T_mean']:8.5f} {r['S_max']:7.4f} {r['S_mean']:8.5f} {r['past_gate']:5d} {r['past_gate_fraction']:4.1%}")
