#!/usr/bin/env python3
"""Run 77 B (run288) docked-load fog hand-over timeline.
Usage: python3 timeline.py [session log]. Output kept beside as timeline_out.txt.
Per frame: elapsed ms, dt, cards (observed/refused/ready/warmup/applied/refusal),
fog frame (applied/reason), for the two save loads (frames 512 and 1844) and
state transitions over the whole session."""
import re, sys, collections
L = sys.argv[1] if len(sys.argv) > 1 else "/tmp/x3-bottleX3-run288/session-20260924-030857-212.log"
kv = lambda s: dict(re.findall(r"(\w+)=(\S+)", s))
fe, cards, fog = {}, {}, {}
single = []
pat = re.compile(r"^(frame_end|volumetric_fog_cards|volumetric_fog_frame|volumetric_fog_handover|volumetric_fog_docked|volumetric_fog_sector|volumetric_fog_cache|capture_armed|loading_phase) ")
with open(L, errors="replace") as f:
    for line in f:
        m = pat.match(line)
        if not m: continue
        d = kv(line); k = m.group(1); fr = int(d.get("frame", -1))
        if k == "frame_end": fe[fr] = d
        elif k == "volumetric_fog_cards": cards[fr] = d
        elif k == "volumetric_fog_frame": fog[fr] = d
        else: single.append((fr, k, line.strip()[:220]))
print("event rows:")
for s in single: print(" ", s[2])
def row(fr):
    e, c, g = fe.get(fr, {}), cards.get(fr, {}), fog.get(fr, {})
    return (f"frame={fr} el={e.get('elapsed_ms')} dt={e.get('dt_ms')} draws={e.get('draws')} | cards obs={c.get('observed')} sup={c.get('suppressed')} ref={c.get('refused')} ready={c.get('ready')} warm={c.get('warmup')} app={c.get('applied')} refusal={c.get('refusal')} reason={c.get('reason')} | fog app={g.get('applied')} reason={g.get('reason')}")
def sig(fr):
    c, g = cards.get(fr, {}), fog.get(fr, {})
    return (c.get('suppressed','0')!='0', c.get('refused'), c.get('ready'), c.get('warmup'), c.get('applied'), c.get('refusal'), g.get('applied'), g.get('reason'))
print("\nstate transitions over the session (first frame of each new state):")
prev = None
for fr in sorted(set(cards) | set(fog)):
    s = sig(fr)
    if s != prev: print(" ", row(fr)); prev = s
print("\nrefusal counts:", collections.Counter(c.get('refusal') for c in cards.values()))
print("fog frame reasons:", collections.Counter((g.get('applied'), g.get('reason')) for g in fog.values()))
for start in (510, 1842):
    print(f"\nframes {start}..{start+60}:")
    for fr in range(start, start + 61):
        if fr in fe or fr in cards: print(" ", row(fr))
print("\nF8 burst frames (fog applied / cards applied):")
for fr in list(range(2428, 2436)) + list(range(3440, 3448)):
    print(" ", row(fr))
print("\ndraws per 100-frame bucket 3500..3800 (undock inferred from the draw drop):")
for b in range(3500, 3800, 50):
    ds = [int(fe[f]['draws']) for f in range(b, b+50) if f in fe]
    print(f"  {b}: avg draws {sum(ds)/len(ds):.0f}")
