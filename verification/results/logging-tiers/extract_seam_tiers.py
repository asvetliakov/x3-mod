#!/usr/bin/env python3
"""Compact record of the seam-log-tiers and seam-log-tiers-emitters cases from a motion-output result file
(verification/results/bottle-X3/motion-output-partial.json, rewritten by every partial run and restored afterwards).
Usage: extract_seam_tiers.py <motion-output-*.json>  -> JSON on stdout."""
import json, sys
d = json.load(open(sys.argv[1]))
tiers = d['cases']['seam-log-tiers']
emitters = d['cases']['seam-log-tiers-emitters']
keep = ('frames', 'steady_rows', 'steady_bytes', 'rows_per_frame', 'bytes_per_frame')
print(json.dumps({
    'seam-log-tiers': {'checks': tiers['checks'], 'exit': tiers['exit'],
                       'always_no_capture': {**{k: tiers['always'][k] for k in keep}, 'steady_names': tiers['always']['steady_names']},
                       'equivalence_names': {k: v['names'] for k, v in tiers['equivalence'].items()},
                       'bench': {k: tiers['bench'][k] for k in ('mean_us', 'max_us') if k in tiers['bench']}},
    'seam-log-tiers-emitters': {'checks': emitters['checks'], 'exit': emitters['exit'],
                                'tiers': {t: {**{k: v.get(k) for k in keep}, 'emitter_rows': v['emitter_rows'],
                                              'steady_frame_names': v['steady_frame_names']} for t, v in emitters['tiers'].items()}}},
    indent=1))
