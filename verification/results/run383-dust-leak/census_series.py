#!/usr/bin/env python3
"""Print the scene_graph_census series of a session log (one line per row): frame, engine_nodes,
registry_live, tasks, the dust-site insert count (pair 00486d7d/0041f332) and the first unattached
histogram bucket. Also prints the volumetric_fog_sector rows (background index of the sector).
Usage: python3 census_series.py <session.log>"""
import re
import sys

for line in open(sys.argv[1], errors='replace'):
    if line.startswith('scene_graph_census '):
        d = dict(re.findall(r'(\w+)=(\S+)', line))
        dust = next((v.split(':')[1] for k, v in d.items()
                     if re.fullmatch(r'i\d', k) and v.startswith('00486d7d/0041f332:')), '0')
        print('census', d.get('frame'), d.get('engine_nodes'), d.get('registry_live'), d.get('tasks'),
              'dust_inserts=' + dust, d.get('b0', '-'))
    elif line.startswith('volumetric_fog_sector '):
        d = dict(re.findall(r'(\w+)=(\S+)', line))
        print('fog_sector', d.get('frame'), 'index=' + d.get('index', '?'), 'reason=' + d.get('reason', '?'))
