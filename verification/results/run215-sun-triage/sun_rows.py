#!/usr/bin/env python3
"""Run215 sun triage: sun/lens/occlusion rows of one or more x3m.log files (counts and first rows only).
Usage: python3 sun_rows.py /mnt/c/X3/x3m.log verification/results/run192-flight/x3m.log ...
"""
import re, sys
ONCE = ('proxy_identity', 'sun_occlusion ', 'sun_occlusion_config', 'sun_occlusion_device', 'sun_flare_fix',
        'lens_flare_gain ', 'lens_flare_gain_device', 'lens_flare_cull', 'sun_lens_body_unclassifiable')
COUNT = ('sun_visibility', 'sun_lens_draw', 'sun_lens_bracket', 'sun_occlusion_blocked', 'sun_occlusion_failed',
         'sun_occlusion_foreign_thread', 'lens_flare_gain_refused', 'emission_source_gain_refused_state', 'bloom_refusal')
for path in sys.argv[1:]:
    print('==', path)
    seen, counts, draws, gained = set(), {k: 0 for k in COUNT}, 0, 0
    with open(path, 'r', errors='replace') as f:
        for line in f:
            for k in ONCE:
                if line.startswith(k) and k not in seen:
                    seen.add(k); print('  ', line[:220].rstrip())
            for k in COUNT:
                if line.startswith(k + ' '): counts[k] += 1
            if line.startswith('lens_flare_gain_frame'):
                m = re.search(r' draws=(\d+) gained=(\d+)', line)
                draws += int(m.group(1)); gained += int(m.group(2))
    print('   counts', counts, 'lens_gain_draws', draws, 'gained', gained)
