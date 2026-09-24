"""Did the camera/ODS actually move inside the F8 capture bursts? Per burst frame: the ODS tower draw's view-matrix rows
and projected object origin (c24-27 translation column, as tools/analysis/crosscheck_motion_origin.py), the displacement
to the previous burst frame in px (5120x1440), plus motion_output_frame camera_rotation_deg for frames around each burst.
usage: burst_motion.py LOG"""
import sys, re, struct, collections
KV = re.compile(r'(\w+)=(\S*)')
W, H = 5120, 1440
def f32(w): return struct.unpack('<f', struct.pack('<I', int(w, 16)))[0]
path = sys.argv[1]
ods = {}
census_frames = set()
for line in open(path, errors='replace'):
    if line.startswith('cull_census device') and 'usc_dock_e_tower' in line:
        d = dict(KV.findall(line)); ods[d['node']] = 1; census_frames.add(int(d['frame']))
rot = {}; view = {}; origin = {}
frame = None; cur_node = None; rows = {}; idx = None; first_view = {}
for line in open(path, errors='replace'):
    if line.startswith('motion_output_frame'):
        d = dict(KV.findall(line)); rot[int(d['frame'])] = (d['camera_rotation_deg'], d['jitter_x'], d['jitter_y'])
        continue
    if line.startswith('draw device=1 '):
        d = dict(KV.findall(line)); frame = int(d['frame']); idx = d['index']; rows = {}; cur_node = None
    elif frame in census_frames:
        if line.startswith('object_context device=1'):
            d = dict(KV.findall(line)); cur_node = d['node'] if d['node'] in ods else None
        elif cur_node and line.startswith('object_matrix role=view row=3') and frame not in view:
            view[frame] = line.split('bits=')[1].strip()
        elif line.startswith('constant kind=vs type=f reg=2'):
            d = dict(KV.findall(line)); r = int(d['reg'])
            if 24 <= r <= 27: rows[r] = [f32(b) for b in d['bits'].split(',')]
        elif cur_node and line.startswith('motion_route device=1') and frame not in origin and len(rows) == 4:
            x, y, z, w = rows[24][3], rows[25][3], rows[26][3], rows[27][3]
            origin[frame] = ((x / w + 1) * 0.5 * W, (1 - y / w) * 0.5 * H)
print(f'== {path}')
prev = None
for f in sorted(census_frames):
    o = origin.get(f)
    dpx = ((o[0] - prev[0]) ** 2 + (o[1] - prev[1]) ** 2) ** 0.5 if (o and prev and f - 1 in origin) else None
    print(f'frame {f} rot={rot.get(f, ("?",))[0]} jitter={rot.get(f, ("?","?","?"))[1:]} tower_origin_px={o and (round(o[0],2), round(o[1],2))} '
          f'd_prev_px={None if dpx is None else round(dpx, 3)} view_row3={view.get(f)}')
    prev = o
# rotation around bursts (frames outside the burst are the flight itself)
starts = sorted(f for f in census_frames if f - 1 not in census_frames)
for s in starts:
    seq = [(f, rot[f][0]) for f in range(s - 12, s + 20) if f in rot]
    print('around', s, ' '.join(f'{f}:{r}' for f, r in seq))
