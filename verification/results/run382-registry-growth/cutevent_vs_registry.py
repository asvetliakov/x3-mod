# Run381: cutevent (script driver 0x0048f550) p50 per 300-frame window vs
# frames since the post-load start of shadow_retention_frame rows, and the
# implied cost per accumulated registry key at 27 keys/frame (run365 slope).
import glob, re
f = sorted(glob.glob("/tmp/x3-bottleX3-run381/session-*.log"))[0]
kv = re.compile(r"(\w+)=(\S+)")
rows = [dict(kv.findall(l)) for l in open(f, errors="replace") if l.startswith("loop_phases ")]
first = None
for l in open(f, errors="replace"):
    if l.startswith("shadow_retention_frame"):
        first = int(dict(kv.findall(l))["frame"]); break
print("first shadow_retention_frame (post-load) frame:", first)
for d in rows:
    fr = int(d["frame"]); c = int(d["cutevent_p50_us"])
    keys = max(0, fr - first) * 27
    print(f"frame={fr} cutevent_p50_us={c} cutevent_p95_us={d['cutevent_p95_us']} est_keys={keys} ns_per_key={c*1000/keys if keys else float('nan'):.1f}")
