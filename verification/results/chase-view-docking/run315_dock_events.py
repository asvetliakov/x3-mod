#!/usr/bin/env python3
"""Measured docking witness for docs/reverse-engineering/chase-view-docking.md.

Streams the local, untracked run315 session log (133 MB, never read whole) and prints only the
derived rows: chase_transition events 50..79 (kind, caller, requested, mode, next_pc, task,
return chain incl. the detail row's extra returns, warp/killed, geometry), the restore
transfer refusal, whether any cockpit lifetime (kind 0/1/2), deserialize (7) or mode store (6)
lies between the dock reset and the first input-driven store, and the static camera span.

usage: python3 run315_dock_events.py [log]
"""
import hashlib, re, sys
LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/x3-bottleX3-run315/session-20260925-004726-212.log"
KV = re.compile(r"(\w+)=(\S+)")
ev, det, refused, cams = {}, {}, [], []
h = hashlib.sha256()
with open(LOG, "rb") as f:
    for raw in f:
        h.update(raw)
        if raw.startswith(b"chase_transition_event event="):
            d = dict(KV.findall(raw.decode("latin1"))); n = int(d["event"])
            if 45 <= n <= 81: ev[n] = d
        elif raw.startswith(b"chase_transition_detail event="):
            d = dict(KV.findall(raw.decode("latin1"))); n = int(d["event"])
            if 45 <= n <= 81: det[n] = d
        elif raw.startswith(b"chase_view_restore_transfer_refused"):
            refused.append(raw.decode("latin1").strip())
        elif raw.startswith(b"camera_state ") and b" frame=9" in raw[:40]:
            d = dict(KV.findall(raw.decode("latin1")))
            fr = int(d["frame"])
            if 9080 <= fr <= 9270: cams.append((fr, d.get("t")))
print("log sha256", h.hexdigest())
for n in sorted(ev):
    d, x = ev[n], det.get(n, {})
    rets = d.get("context_returns", "") + ("," + x["context_returns_extra"] if x.get("context_returns_extra") else "")
    rets = ",".join(r.split(":")[1].lstrip("0") or "0" for r in rets.split(",") if ":" in r and r != "00000000:00000000")
    print("event %d kind=%s qpc=%s gen=%s caller=%s req=%s mode=%s connect=%s task=%s next_pc=%s flags=%s returns=%s%s"
          % (n, d["kind"], d["qpc"], d["generation"], d["caller"], d["requested"], d["mode"], d["connect"], d.get("task"),
             d["next_pc"], d["origin_flags"], rets,
             (" warp=%s killed=%s offset_160=%s lock_120=%s" % (x.get("warp_phase"), x.get("killed"), x.get("offset_160"), x.get("view_lock_120"))) if x else ""))
for r in refused:
    print(r[:200])
dock = [n for n in ev if ev[n]["caller"] == "0x0042e742" and "9bec3" in (det.get(n, {}).get("context_returns_extra", ""))]
first_input = min(n for n in ev if ev[n]["next_pc"] == "0x000f0c63" and n > max(dock))
between = [n for n in ev if max(dock) < n < first_input and ev[n]["kind"] in ("0", "1", "2", "6", "7")]
print("dock reset events", dock, "first input-driven store", first_input, "lifetime/mode events between:", between,
      "seconds dock->input %.3f" % ((int(ev[first_input]["qpc"]) - int(ev[max(dock)]["qpc"])) / 1e7))
static = [fr for i, (fr, t) in enumerate(cams[1:], 1) if t == cams[i - 1][1]]
print("camera_state static frames %d..%d (%d), first moving frame after: %s"
      % (min(static), max(static), len(static), next(fr for fr, t in cams if fr > max(static))))
