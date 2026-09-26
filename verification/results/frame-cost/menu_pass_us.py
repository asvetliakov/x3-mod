#!/usr/bin/env python3
"""Menu-plateau frames (frame_end draws >= 500): p50 of dt (qpc) and of the proxy's per-frame CPU spans
(hdr_frame writeback/readback/meter, motion_output_frame taa_run). Used to compare 1920x1080 with 5120x1440
on the same scene class. Usage: menu_pass_us.py <session.log>"""
import re, sys
path = sys.argv[1]; menu = set(); q = {}; vals = {}
FIELDS = {'hdr_frame': ('writeback_us', 'readback_us', 'meter_us'), 'motion_output_frame': ('taa_run_us', 'fill_us')}
with open(path, errors='replace') as fh:
    for line in fh:
        k = line.split(' ', 1)[0]
        if k == 'frame_end':
            m = re.search(r'frame=(\d+) draws=(\d+) capture=(\d+).* qpc=(\d+)', line)
            if m:
                f = int(m.group(1)); q[f] = int(m.group(4))
                if int(m.group(2)) >= 500 and m.group(3) == '0': menu.add(f)
        elif k in FIELDS:
            f = int(re.search(r'\bframe=(\d+)', line).group(1))
            for fld in FIELDS[k]:
                m = re.search(r'\b' + fld + r'=([0-9.]+)', line)
                if m: vals.setdefault(k + '.' + fld, {})[f] = float(m.group(1))
def p50(v): v = sorted(v); return v[len(v) // 2] if v else float('nan')
dts = [(q[f] - q[f - 1]) / 1e4 for f in menu if f - 1 in q]
print(path.split('/')[2], 'menu frames', len(menu), 'dt_p50_ms %.2f' % p50(dts))
for k, d in sorted(vals.items()):
    print('  %-34s p50_us %8.1f' % (k, p50([d[f] for f in menu if f in d])))
