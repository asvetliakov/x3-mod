# Run382: post-load mutation budget. revision at disable (720152) minus the
# load-frame burst vs the sampled per-frame mutation_delta mean, and the
# net live growth per frame. Also run365 live slope from object_lifetime_stats.
import glob, re, statistics
kv = re.compile(r"(\w+)=(\S+)")
def rows(run, prefix):
    f = sorted(glob.glob(f"/tmp/x3-bottleX3-{run}/session-*.log"))[0]
    return [dict(kv.findall(l)) for l in open(f, errors="replace") if l.startswith(prefix)]
s = rows("run382", "shadow_retention_frame ")
pre = [int(d["mutation_delta"]) for d in s if int(d["frame"]) < 10079]
print("run382 sampled rows before disable:", len(pre), "first frame", s[0]["frame"], "mean", round(statistics.mean(pre),1), "median", statistics.median(pre), "max", max(pre))
first = int(s[0]["frame"])
frames = 10079 - first
print(f"run382 frames first_row..disable = {frames}; live growth/frame = {262144/frames:.1f}; revision/frame if load burst were 274908 (run365/run375 value) = {(720152-274908)/frames:.1f}")
st = rows("run365", "object_lifetime_stats ")
pts = [(int(d["frame"]), int(d["live"])) for d in st if 900 <= int(d["frame"]) <= 3300]
slope = (pts[-1][1]-pts[0][1])/(pts[-1][0]-pts[0][0])
print("run365 live series:", " ".join(f"{d['frame']}:{d['live']}" for d in st), f"\nrun365 slope f900..f3300 = {slope:.2f} keys/frame; menu f300..f600 slope = 0")
