"""Per-cascade footprint census (shadow_replay_candidates), live casters (shadow_retention_frame)
and replay cost (shadow_replay_depth) for run253 / run251 (footprint 8) and run250 (no footprint)."""
import re
LOGS = {"run253": "/tmp/x3-bottleX3-run253/session-20260922-232511-212.log",
        "run251": "/tmp/x3-bottleX3-run251/session-20260922-231055-216.log",
        "run250": "/tmp/x3-bottleX3-run250/session-20260922-230123-216.log"}
kv = re.compile(r"(\w+)=(\S+)")
PFX = {"shadow_replay_candidates ": r"(footprint_refused|footprint_aged|dropped_min_size|capped)\d$",
       "shadow_retention_frame ": r"live_c\d$",
       "shadow_replay_depth ": r"(draws\d?|us|apply_us|issues|replayed)$"}
for name, path in LOGS.items():
    tot = {}; n = {}; nz = {}; pairs = []; opts = ""
    for line in open(path, errors="replace"):
        if line.startswith("proxy_options") and not opts:
            opts = " ".join(t for t in line.split() if "CASCADE" in t or "FOOTPRINT" in t)
        for p, rx in PFX.items():
            if not line.startswith(p): continue
            d = dict(kv.findall(line)); n[p] = n.get(p, 0) + 1
            for k, v in d.items():
                if re.match(rx, k):
                    try: x = float(v)
                    except ValueError: continue
                    tot[(p, k)] = tot.get((p, k), 0) + x; nz[(p, k)] = nz.get((p, k), 0) + (x != 0)
            if p == "shadow_replay_depth " and "draws" in d and "us" in d:
                pairs.append((float(d["draws"]), float(d["us"])))
    print("==", name, opts)
    for p in PFX:
        print(" ", p.strip(), "rows", n.get(p, 0))
        for (pp, k), v in sorted(tot.items()):
            if pp == p: print("    %-20s mean %9.2f  nonzero %d" % (k, v / n[p], nz[(pp, k)]))
    if pairs:
        import statistics as s
        xs = [a for a, b in pairs]; ys = [b for a, b in pairs]
        mx, my = s.mean(xs), s.mean(ys)
        sl = sum((a - mx) * (b - my) for a, b in pairs) / max(1e-9, sum((a - mx) ** 2 for a in xs))
        print("    replay us/draw: ratio-of-means %.3f  OLS slope %.3f  intercept %.1f  median us %.1f  p95 us %.1f" % (
            my / mx, sl, my - sl * mx, s.median(ys), sorted(ys)[int(.95 * len(ys))]))
