"""Per-cascade footprint_refused / footprint_aged / dropped_min_size / capped from shadow_replay_candidates,
run251 vs run250; plus the capture frames 9772-9779 and 11400-11407 of run251."""
import re
LOGS = {"run251": "/tmp/x3-bottleX3-run251/session-20260922-231055-216.log",
        "run250": "/tmp/x3-bottleX3-run250/session-20260922-230123-216.log"}
kv = re.compile(r"(\w+)=(\S+)")
CAP = set(range(9772, 9780)) | set(range(11400, 11408))
for name, path in LOGS.items():
    tot = {}; nz = {}; n = 0; keys = None; capt = []
    for line in open(path, errors="replace"):
        if not line.startswith("shadow_replay_candidates "): continue
        d = dict(kv.findall(line)); n += 1
        if keys is None:
            keys = [k for k in d if re.match(r"(footprint_refused|footprint_aged|dropped_min_size|capped|admitted|accepted|dropped)\w*\d$", k)]
            print(name, "fields:", " ".join(keys))
        for k in keys:
            v = int(float(d[k])); tot[k] = tot.get(k, 0) + v; nz[k] = nz.get(k, 0) + (v != 0)
        if name == "run251" and int(d["frame"]) in CAP:
            capt.append((d["frame"], [d[k] for k in keys if k.startswith("footprint_refused")]))
    print(name, "rows", n)
    for k in keys: print("  %-22s mean %.2f  frames_nonzero %d" % (k, tot[k] / n, nz[k]))
    for f, v in capt: print("  capture frame", f, "footprint_refused0..4", v)
