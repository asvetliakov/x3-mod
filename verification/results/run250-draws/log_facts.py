#!/usr/bin/env python3
"""Run250 facts not in the other outputs: the lod_scale rows, and the stand's camera distance from run248's
stand (camera_state at run250 frame 7357 vs run248 frame 8055; eye = -R^T t and |t_a - t_b| both printed,
distance in game units). Usage: log_facts.py [run250 log] [run248 log]"""
import math, re, sys
A = sys.argv[1] if len(sys.argv) > 1 else "/tmp/x3-bottleX3-run250/session-20260922-230123-216.log"
B = sys.argv[2] if len(sys.argv) > 2 else "/tmp/x3-bottleX3-run248/session-20260922-202645-472.log"
kv = re.compile(r"(\w+)=(\S+)")
def cam(path, frame, lod=False):
    want = "camera_state device=1 frame=%d " % frame
    with open(path, errors="replace") as fh:
        for line in fh:
            if lod and line.startswith("lod_scale"): print(line.split(" device=")[0].strip()[:160])
            if line.startswith(want):
                d = dict(kv.findall(line)); t = [float(x) for x in d["t"].split(",")]
                r = [[float(d["r%d%d" % (i, j)]) for j in range(3)] for i in range(3)]
                eye = [-sum(r[i][j] * t[i] for i in range(3)) for j in range(3)]  # t = -R eye (column form)
                eye_row = [-sum(r[j][i] * t[i] for i in range(3)) for j in range(3)]  # t = -eye R (D3D row form)
                return t, eye, eye_row
t1, e1, f1 = cam(A, 7357, lod=True); t0, e0, f0 = cam(B, 8055)
print("eye distance run248 f8055 -> run250 f7357: column form %.0f, row form %.0f units" % (math.dist(e0, e1), math.dist(f0, f1)))
print("|t| difference: %.0f units" % math.dist(t0, t1))
