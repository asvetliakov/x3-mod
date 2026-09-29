# Per-run series of registry-derived counters from shadow_retention_frame rows
# and object_lifetime_stats / disabled rows. Usage: python3 series.py
import glob, re, statistics
runs = ["run365", "run379", "run380", "run381", "run382"]
kv = re.compile(r"(\w+)=(\S+)")
for r in runs:
    f = sorted(glob.glob(f"/tmp/x3-bottleX3-{r}/session-*.log"))[0]
    rows, loads = [], []
    for line in open(f, errors="replace"):
        if line.startswith("shadow_retention_frame"):
            d = dict(kv.findall(line))
            rows.append((int(d["frame"]), int(d["mutation_delta"]), int(d["retired"]), d.get("flush"), int(d["journal_overflow"])))
        elif line.startswith("object_lifetime_disabled"):
            print(r, "DISABLED", line.strip()[:200])
    if not rows:
        print(r, "no shadow_retention_frame rows"); continue
    md = [x[1] for x in rows]
    print(f"{r}: rows={len(rows)} frames {rows[0][0]}..{rows[-1][0]} mutation_delta min/median/max={min(md)}/{statistics.median(md)}/{max(md)} retired_sum={sum(x[2] for x in rows)} journal_overflow_sum={sum(x[4] for x in rows)}")
    flushes = {}
    for x in rows: flushes[x[3]] = flushes.get(x[3], 0) + 1
    print("   flush:", flushes)
    step = max(1, len(rows)//12)
    print("   sample (frame:delta:retired):", " ".join(f"{x[0]}:{x[1]}:{x[2]}" for x in rows[::step]))
