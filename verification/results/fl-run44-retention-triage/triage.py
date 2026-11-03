#!/usr/bin/env python3
"""FL run 44 retention triage: why the retained station casts no hull shadow.
Usage: triage.py LOG CAPTURE_DIR   (log /home/asvetl/x3m-logs/fl-run44-retention.log,
captures /mnt/c/X3FL/x3-modern-captures). Prints per capture frame: station draws by
alpha_tested (object_bounds), retained station records (shadow_retention_caster, model 5597
node 52942), live/retained replay counts, and shadow-map coverage per cascade."""
import re, sys, collections
import numpy as np
log, cap = sys.argv[1], sys.argv[2].rstrip('/') + '/'
CAPS = ('21221', '28119', '28990', '29793')
pat = re.compile(r'^(object_bounds|shadow_retention_caster|shadow_retention_frame|shadow_replay_caster) .* frame=(%s) ' % '|'.join(CAPS))
ob = collections.Counter(); ret = collections.Counter(); rf = {}; rc = collections.Counter()
with open(log, errors='replace') as fh:
    for l in fh:
        m = pat.match(l)
        if not m: continue
        kind, f = m.groups()
        d = dict(x.split('=', 1) for x in l.split()[1:] if '=' in x)
        if kind == 'object_bounds' and d.get('model') == '00005597':
            ob[(f, 'alpha' if d.get('alpha_tested') == '1' else 'opaque')] += 1
        elif kind == 'shadow_retention_caster' and d.get('handle') == '52942' and d.get('unseen') != '0':
            ret[f] += 1
        elif kind == 'shadow_replay_caster' and d['origin'].startswith('-121472'):
            rc[(f, 'retained' if d['retained'] == '1' else 'live')] += 1
        elif kind == 'shadow_retention_frame':
            rf[f] = {k: d[k] for k in ('nodes_unseen', 'records_unseen', 'live_c0', 'would_c0', 'would_c3', 'excluded_class', 'buffer_gone', 'lod_replaced', 'superseded')}
for f in CAPS:
    cov = []
    for c, s in ((0, 2048), (1, 4096), (2, 4096), (3, 4096), (4, 2048)):
        a = np.fromfile(cap + 'shadow_map%d_1_%s.r32f' % (c, f), dtype=np.float32).reshape(s, s)
        cov.append('%.4f' % (a < 1).mean())
    print(f, 'station_draws opaque=%d alpha=%d' % (ob[(f, 'opaque')], ob[(f, 'alpha')]),
          'station_replay live=%d retained=%d' % (rc[(f, 'live')], rc[(f, 'retained')]),
          'station_records_unseen=%d' % ret[f], rf.get(f), 'map_cov=' + ','.join(cov))
