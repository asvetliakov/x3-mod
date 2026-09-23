#!/usr/bin/env python3
"""Run-length summary of per-frame fog state (volumetric_fog_frame + volumetric_fog_cards + cache_frame).
Usage: fog_runs.py session.log  -> prints one row per change of (applied, reason, cards_state)."""
import sys, re
kv = re.compile(r'(\w+)=(\S+)')
st = {}
frame_ms = {}
prev = None; start = None
out = []
def emit(f):
    out.append((start, f, prev))
for line in open(sys.argv[1], errors='replace'):
    t = line.split(' ', 1)[0]
    if t not in ('volumetric_fog_frame', 'volumetric_fog_cards', 'frame_end', 'volumetric_fog_cache_frame'):
        continue
    d = dict(kv.findall(line))
    f = int(d.get('frame', -1))
    if t == 'frame_end':
        frame_ms[f] = int(d['elapsed_ms'])
        continue
    if t == 'volumetric_fog_frame':
        st[f] = dict(st.get(f, {}), applied=d['applied'], reason=d['reason'], profile=d['profile'])
    elif t == 'volumetric_fog_cache_frame':
        st[f] = dict(st.get(f, {}), rf=('far0' if float(d['ready_far']) < 1 else 'far1'))
    else:
        s = 'obs' if int(d['observed']) else '-'
        s += '/sup' if int(d['suppressed']) else ''
        s += '/ref' if int(d['refused']) else ''
        s += '/warm' if int(d['warmup']) else ''
        st[f] = dict(st.get(f, {}), cards=s, card_reason=d['reason'], mode=d['mode'])
frames = sorted(st)
for f in frames:
    s = st[f]
    k = (s.get('applied'), s.get('reason'), s.get('profile'), s.get('cards'), s.get('card_reason'), s.get('mode'), s.get('rf'))
    if k != prev:
        if prev is not None: emit(lastf)
        prev = k; start = f
    lastf = f
emit(lastf)
for a, b, k in out:
    print(f"frames {a}-{b} ({b-a+1}) ms {frame_ms.get(a)}-{frame_ms.get(b)} applied={k[0]} reason={k[1]} profile={k[2]} cards={k[3]} card_reason={k[4]} mode={k[5]} {k[6]}")
