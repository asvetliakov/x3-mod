#!/usr/bin/env python3
"""HDR exposure readback lock (hdr_frame readback_transfer_lock_us) per frame, joined with frame_end dt (qpc delta,
0.1 us ticks), draws and the frame_dt.py state (menu / flight_bgN / flight_noreplay). Prints p10/p50/p90 of lock,
dt, dt-lock and draws per state, Pearson r(lock, dt), r(lock, draws), and flight lock by draw bin.
Usage: readback_lock.py <session.log>"""
import re, sys, math
path = sys.argv[1]
fe = {}; hdr = {}; replay = set(); sect = []
rfe = re.compile(r'frame=(\d+) draws=(\d+) capture=(\d+) .* qpc=(\d+)')
rh = re.compile(r' frame=(\d+) .*redirected=(\d+) .* writeback_us=([\d.]+) .* meter_us=([\d.]+) readback_us=([\d.]+) readback_transfer_lock_us=([\d.]+)')
with open(path, errors='replace') as fh:
    for line in fh:
        if line.startswith('frame_end '):
            m = rfe.search(line)
            if m: fe[int(m.group(1))] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
        elif line.startswith('hdr_frame '):
            m = rh.search(line)
            if m: hdr[int(m.group(1))] = (int(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(6)))
        elif line.startswith('shadow_replay_depth '):
            replay.add(int(re.search(r'frame=(\d+)', line).group(1)))
        elif line.startswith('volumetric_fog_sector '):
            m = re.search(r'frame=(\d+).*reason=(\S+).* index=(-?\d+)', line)
            if m: sect.append((int(m.group(1)), m.group(2), int(m.group(3))))
sect.sort(); frames = sorted(fe); si = 0; cur = ('none', -1); G = {}
for i in range(1, len(frames)):
    f, p = frames[i], frames[i - 1]
    if f != p + 1 or f not in hdr: continue
    while si < len(sect) and sect[si][0] <= f: cur = (sect[si][1], sect[si][2]); si += 1
    draws, capt, q = fe[f]; dt = (q - fe[p][2]) / 1e4
    if capt or draws == 0: continue
    menu = cur[0].startswith('no_cockpit') or cur[0] == 'none'
    k = 'menu' if menu else ('flight_bg%d' % cur[1] if f in replay else 'flight_noreplay')
    lock = hdr[f][3] / 1000.
    for kk in (k, 'flight_all') if k.startswith('flight_bg') else (k,):
        G.setdefault(kk, []).append((lock, dt, draws, hdr[f][1] / 1000.))
def pct(v, q): v = sorted(v); return v[min(len(v) - 1, int(q * (len(v) - 1) + 0.5))]
def r(a, b):
    n = len(a); ma, mb = sum(a) / n, sum(b) / n
    sa = math.sqrt(sum((x - ma) ** 2 for x in a)); sb = math.sqrt(sum((y - mb) ** 2 for y in b))
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (sa * sb) if sa and sb else float('nan')
print('file', path)
print('%-16s %6s | %-17s | %-17s | %-17s | %-11s | %6s %6s' % ('state', 'n', 'lock ms p10/50/90', 'dt ms p10/50/90', 'dt-lock p10/50/90', 'draws p50', 'r(l,dt)', 'r(l,dr)'))
for k in sorted(G):
    v = G[k]
    if len(v) < 30: continue
    L = [x[0] for x in v]; D = [x[1] for x in v]; N = [x[2] for x in v]; R = [x[1] - x[0] for x in v]
    f3 = lambda s: '%5.2f %5.2f %5.2f' % (pct(s, .1), pct(s, .5), pct(s, .9))
    print('%-16s %6d | %s | %s | %s | %11d | %6.2f %6.2f' % (k, len(v), f3(L), f3(D), f3(R), pct(N, .5), r(L, D), r(L, N)))
v = G.get('flight_all', [])
if v:
    print('flight_all by draw bin: bin n lock_p50 dt_p50 dt-lock_p50')
    for lo in range(0, 800, 50):
        s = [x for x in v if lo <= x[2] < lo + 50]
        if len(s) >= 30:
            print('  %3d-%3d %6d %6.2f %6.2f %6.2f' % (lo, lo + 49, len(s), pct([x[0] for x in s], .5), pct([x[1] for x in s], .5), pct([x[1] - x[0] for x in s], .5)))
