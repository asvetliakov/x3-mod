#!/usr/bin/env python3
"""Run 201 A triage of C:\\X3\\x3m.log (prints matching rows and counts only)."""
import re, sys, collections, statistics
LOG = sys.argv[1] if len(sys.argv) > 1 else "/mnt/c/X3/x3m.log"
PREV = sys.argv[2] if len(sys.argv) > 2 else "/mnt/c/X3/x3m.prev.log"
L = open(LOG, encoding="utf-8", errors="replace").read().splitlines()
def kind(l):
    t = l.split()
    for w in t:
        if "=" not in w and not w.startswith("[") and not re.match(r"^[\d:.\-T]+$", w):
            return w
    return t[0] if t else ""
def show(title, pat, cap=40):
    r = [l for l in L if re.search(pat, l)]
    print(f"== {title}: {len(r)}")
    for l in r[:cap]: print("  ", l[:260])
print("identity:", [l[:120] for l in L if l.startswith("proxy_identity")])
print("== 1 install/arming")
show("music_queue armed", r"music_queue\b.*armed=")
show("music_queue_kc", r"music_queue_kc action=")
show("music_dock_keep", r"music_dock_keep")
show("seta_manual_flight installed", r"seta_manual_flight installed=")
show("seta_key_latch", r"seta_key_latch")
show("seta_restore installed", r"seta_restore installed")
show("fog distance mode", r"volumetric_fog_distance_mode")
show("horizon_ready", r"horizon_ready")
bad = r"refused|refuse|bytes_mismatch|fallback|horizon_fallback|card_fault|prepare_oom|prepare_retry|exception|crash"
c = collections.Counter()
for l in L:
    for m in set(re.findall(bad, l)): c[(kind(l), m)] += 1
print("== bad-word rows by kind:", dict(c))
show("bad-word rows", bad, 60)
print("== 2 music")
ev = collections.Counter()
for l in L:
    m = re.search(r"music_queue event=(\S+)", l)
    if m:
        r = re.search(r"reason=(\S+)", l); ev[(m.group(1), r.group(1) if r else "-")] += 1
for k, v in sorted(ev.items()): print("  ", k, v)
show("music_queue all", r"music_queue", 120)
print("== 3 fog")
show("fog timing rows", r"volumetric_fog_prefill|far_ready|fine_ready|horizon_ready|volumetric_fog_memory", 80)
for key in ("far_ready", "fine_ready", "horizon_ready"):
    vals = []
    for l in L:
        if key in l:
            m = re.search(r"\b(?:ms|elapsed_ms|%s_ms)=(\d+(?:\.\d+)?)" % key, l)
            if m: vals.append(float(m.group(1)))
    print(f"  {key} n={len(vals)} median={statistics.median(vals) if vals else None} vals={vals}")
print("== 4 frame time")
show("dt/frame-time rows", r"dt_p50|frame_time|frame_ms|stall|hitch", 80)
print("== 5 kinds new vs prev")
P = open(PREV, encoding="utf-8", errors="replace")
pk = collections.Counter(kind(l) for l in P)
ck = collections.Counter(kind(l) for l in L)
print("prev identity:", [l[:120] for l in open(PREV, errors="replace").read(4000).splitlines() if l.startswith("proxy_identity")])
print("new kinds:", {k: v for k, v in ck.items() if k not in pk})
print("all kinds:", dict(ck.most_common()))
print("== 4b frame_end windows (avg ms/frame over 3600 frames)")
prev = None
for l in L:
    if l.startswith("frame_end"):
        f = int(re.search(r" frame=(\d+)", l).group(1)); d = int(re.search(r"dt_ms=(\d+)", l).group(1))
        if prev is not None and f > prev: print(f"  frames {prev}..{f} dt_ms={d} avg_ms={d/(f-prev):.2f} fps={1000*(f-prev)/d:.0f}")
        prev = f
show("music_keep rows", r"^music_keep_(stop|seek|save_pump) ", 40)
show("seta rows", r"^seta_", 40)
show("session_end", r"^session_end")
