"""Summarise the stored-density fog cache rows of an x3m.log (grep-style; never loads the file whole).
Usage: python3 -I log_rows.py /mnt/c/X3/x3m.log"""
import re, sys, statistics as st
epochs, far, fine, fill, rng, prefill = {}, [], [], [], None, 0
for line in open(sys.argv[1], errors="replace"):
    if "volumetric_fog" not in line:
        continue
    if line.startswith("volumetric_fog_prefill"):
        prefill += 1
    elif line.startswith("volumetric_fog_range"):
        rng = line.strip()
    elif line.startswith("volumetric_fog_cache") and "event=epoch" in line:
        r = re.search(r"reason=(\w+)", line).group(1); epochs[r] = epochs.get(r, 0) + 1
    elif line.startswith("volumetric_fog_cache") and "event=far_ready" in line:
        far.append(float(re.search(r" ms=([\d.]+)", line).group(1)))
    elif line.startswith("volumetric_fog_cache") and "event=fine_ready" in line:
        fine.append(float(re.search(r" ms=([\d.]+)", line).group(1)))
    elif line.startswith("volumetric_fog_handover ") and "fill_ms=" in line:
        fill.append(float(re.search(r"fill_ms=([\d.]+)", line).group(1)))
    last_nodes = re.search(r"nodes=(\d+) worker_busy_ms=([\d.]+)", line)
    if last_nodes:
        nodes, busy = int(last_nodes.group(1)), float(last_nodes.group(2))
print(rng)
print("prefill_rows", prefill, "epochs", epochs)
for name, v in (("far_ready_ms", far), ("fine_ready_ms", fine), ("cold_far_fill_ms", fill)):
    print(name, "n", len(v), "min", min(v), "median", st.median(v), "max", max(v))
print("worker_nodes", nodes, "busy_ms", busy, "rate_Mnodes_per_s %.2f" % (nodes / busy / 1e3))
# Residency epochs versus device Reset: an hdr_target (re)creation row in the same frame.
res, hdr = set(), set()
for line in open(sys.argv[1], errors="replace"):
    if "event=epoch reason=residency" in line:
        res.add(re.search(r"frame=(\d+)", line).group(1))
    elif line.startswith("hdr_target "):
        hdr.add(re.search(r"frame=(\d+)", line).group(1))
print("residency_epochs", len(res), "same_frame_as_hdr_target_create", len(res & hdr))
