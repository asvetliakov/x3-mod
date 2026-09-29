# Per-frame registry mutation rate (shadow_retention_frame mutation_delta,
# load-frame burst excluded) on stock-tree runs (Run100 A = run355/356 and
# earlier) vs Mayhem 3 runs with a live observer (run365+). Runs 358-364 are
# Mayhem with the observer disabled at load (16384 table): delta 0 there means nothing.
import glob, re, statistics
kv = re.compile(r"(\w+)=(\S+)")
for r in ["run340", "run352", "run356", "run365", "run368", "run375", "run379", "run382"]:
    fs = sorted(glob.glob(f"/tmp/x3-bottleX3-{r}/session-*.log"))
    if not fs: print(r, "missing"); continue
    md = [int(dict(kv.findall(l))["mutation_delta"]) for l in open(fs[0], errors="replace") if l.startswith("shadow_retention_frame ")]
    md = [x for x in md if x < 10000]
    dis = sum(1 for l in open(fs[0], errors="replace") if l.startswith("object_lifetime_disabled"))
    print(f"{r}: rows={len(md)} mean={statistics.mean(md):.1f} median={statistics.median(md)} min={min(md)} max={max(md)} disabled_rows={dis}")
