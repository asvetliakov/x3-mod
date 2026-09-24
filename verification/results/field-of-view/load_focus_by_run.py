#!/usr/bin/env python3
"""field-of-view.md §7.4. Usage: load_focus_by_run.py /tmp/x3-bottleX3-run3NN/session-*.log ...
Per session log: each save_load_complete frame, the last sector reason logged before the sample, and the scene F
(from camera_state p11: F = 2*atan(1/(0.75*p11)) in binary-angle units) at the first valid camera_state
10 frames later. Prints one line per load."""
import re, sys, math, glob
kv = re.compile(rb'(\w+)=(\S+)')
for L in sys.argv[1:]:
    loads = []; pending = None; last_sector = None
    with open(L, 'rb') as f:
        for raw in f:
            if raw.startswith(b'loading_phase name=save_load_complete'):
                fr = int(re.search(rb' frame=(\d+)', raw).group(1)); loads.append([fr, None, None]); pending = loads[-1]
            elif raw.startswith(b'volumetric_fog_sector ') and b'reason=no_cockpit' not in raw:
                last_sector = dict(kv.findall(raw)).get(b'reason', b'?').decode()
            elif raw.startswith(b'camera_state device=') and pending and pending[2] is None:
                d = dict(kv.findall(raw))
                if d.get(b'valid') == b'1' and int(d[b'frame']) >= pending[0] + 10:
                    p11 = float(d[b'p11']); F = 2 * math.atan(1 / (0.75 * p11)) / (2 * math.pi) * 65536
                    pending[2] = (int(d[b'frame']), round(F)); pending[1] = last_sector
    for fr, sec, cam in loads:
        print(L.split('/')[2], L.split('/')[-1][:30], f'load_complete frame={fr} sector={sec} scene_F@{cam[0] if cam else "-"}=0x{cam[1]:04x}' if cam else f'load_complete frame={fr} sector={sec} scene_F=-')
