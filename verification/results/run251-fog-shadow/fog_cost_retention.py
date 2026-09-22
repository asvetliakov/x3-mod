"""Run251 (fog shadow pass on, min-footprint 8) vs run250 (pass off): fog cpu_us/calls,
frame_timing windows, shadow retention capped/footprint per cascade. Prints summaries only."""
import re, statistics as st, sys
LOGS = {"run251": "/tmp/x3-bottleX3-run251/session-20260922-231055-216.log",
        "run250": "/tmp/x3-bottleX3-run250/session-20260922-230123-216.log"}
kv = re.compile(r"(\w+)=(\S+)")
def pct(a, p): a = sorted(a); return a[min(len(a)-1, int(p*len(a)))] if a else None
for name, path in LOGS.items():
    fog = []; ft = []; ret = {}; nret = 0; calls = {}; cap = {}
    for line in open(path, errors="replace"):
        if line.startswith("volumetric_fog_frame "):
            d = dict(kv.findall(line))
            if d.get("applied") == "1":
                fog.append((int(d["frame"]), float(d["cpu_us"]), int(d["calls"])))
                calls[d["calls"]] = calls.get(d["calls"], 0) + 1
        elif line.startswith("frame_timing "):
            d = dict(kv.findall(line)); ft.append((int(d["frame"]), d["dt_p50_us"], d["dt_p95_us"], d["draws_p50"], d["draw_p50_us"], d["gap_draw_p50_us"]))
        elif line.startswith("shadow_retention_frame "):
            d = dict(kv.findall(line)); nret += 1
            for c in range(5):
                for k in ("live_c%d" % c, "capped_c%d" % c, "footprint_refused%d" % c, "footprint_aged%d" % c):
                    if k in d: ret.setdefault(k, []).append(int(d[k]))
        elif line.startswith("shadow_cascade_set "):
            d = dict(kv.findall(line)); cap[d.get("caps")] = cap.get(d.get("caps"), 0) + 1
    print("==", name, "fog applied frames", len(fog), "calls hist", calls)
    cpu = [c for _, c, _ in fog[200:]]
    print(" fog cpu_us (skip first 200 applied) p50 %.1f p95 %.1f mean %.1f" % (pct(cpu, .5), pct(cpu, .95), st.mean(cpu)))
    # per 300-frame window
    w = {}
    for f, c, _ in fog: w.setdefault(f // 300 * 300, []).append(c)
    print(" fog cpu_us p50 per 300-frame window:", " ".join("%d:%.0f" % (k, pct(v, .5)) for k, v in sorted(w.items())))
    print(" frame_timing (frame dt_p50 dt_p95 draws_p50 draw_p50_us gap_draw_p50):")
    for r in ft: print("  ", *r)
    print(" retention frames", nret, "cascade caps", cap)
    for c in range(5):
        row = []
        for k in ("live_c%d", "capped_c%d", "footprint_refused%d", "footprint_aged%d"):
            v = ret.get(k % c)
            row.append("%s mean %.1f max %d nz %d" % (k % c, st.mean(v), max(v), sum(1 for x in v if x)) if v else "%s absent" % (k % c))
        print("  ", " | ".join(row))
