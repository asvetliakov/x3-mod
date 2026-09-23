#!/usr/bin/env python3
"""Sector identity at run273's fogged sector entries (Run75 transit-identity fix): per fogged volumetric_fog_sector
row, the token and background index, and the id [sector+8] from that stall's first found prefill poll of that node.
Usage: transit_identity.py session.log (reads only matching rows; the log is 325 MB)."""
import re, sys
polls, rows = {}, []
for line in open(sys.argv[1], errors='replace'):
    if line.startswith('volumetric_fog_prefill ') and ' walk=found ' in line:
        m = re.search(r'frame=(\d+) .* node=([0-9a-f]+) id=(\d+) index=(-?\d+)', line)
        if m: polls.setdefault((m.group(2), m.group(4)), m.group(3))
    elif line.startswith('volumetric_fog_sector ') and ' profile=0 ' not in line:
        m = re.search(r'frame=(\d+) profile=(\d+) reason=(\S+) sector=([0-9a-f]+) index=(-?\d+)', line)
        if m: rows.append(m.groups())
for frame, profile, family, sector, index in rows:
    print('frame=%s family=%s profile=%s token=%s index=%s id=%s' % (frame, family, profile, sector, index, polls.get((sector, index), 'unread')))
