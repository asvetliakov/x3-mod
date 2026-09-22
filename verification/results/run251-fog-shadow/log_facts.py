#!/usr/bin/env python3
"""Run251 facts not in the other outputs: the volumetric_fog_shadow_pass option row; counts of any row whose
type names a fog grid or fallback; applied volumetric_fog_frame rows whose result or restore is not 00000000
(S_OK; applied rows carry 0, unapplied rows 1: a nonzero HRESULT on an applied row is a failed device call); and the capture files per burst frame in the run directory.
Usage: log_facts.py [run dir]"""
import os, re, sys
from collections import Counter
D = sys.argv[1] if len(sys.argv) > 1 else "/tmp/x3-bottleX3-run251"
LOG = [os.path.join(D, f) for f in os.listdir(D) if f.startswith("session-") and f.endswith(".log")][0]
types = Counter(); bad = 0; applied = 0
with open(LOG, errors="replace") as fh:
    for line in fh:
        t = line.split(" ", 1)[0]
        if t.startswith("volumetric_fog_shadow_pass"): print(line.strip())
        if ("fog" in t or "shadow_pass" in t) and ("grid" in t or "fallback" in t): types[t] += 1
        if t == "volumetric_fog_frame" and " applied=1 " in line:
            applied += 1
            if " result=00000000 " not in line or " restore=00000000 " not in line: bad += 1
print("fog grid/fallback row types:", dict(types) or "none")
print("applied fog frames %d, with result or restore != 00000000: %d" % (applied, bad))
frames = Counter(m.group(1) for f in os.listdir(D) for m in [re.search(r"_(\d{1,6})\.[a-z0-9]+$", f)] if m)
print("capture files per frame:", " ".join("%s:%d" % (k, v) for k, v in sorted(frames.items(), key=lambda x: int(x[0]))))
