"""FL 69 A "the lathe darkened the whole hull" (docs/verification/weapon-effects.md, 2026-11-12): the figures.
(1) The native record's FL 69 A rows (LATHE flight_spread / flight_64_hits / bright_hull / bright_hull_taa / past_life,
    TIMING titan_flight_*). (2) The flight logs (local, untracked): per log whether it carries the lathe counters, the
    hdr_frame exposure (EV against the +1.3 rest value, how often the p99 tile maximum sits at the 64 meter clip) and the
    EV beside the weapon_effects_frame rows the brief quotes. (3) The user's screenshot: 8-bit luma percentiles of the
    honeycomb, of the Titan's hull without cells, of the sunlit station and of the sky, and the sky beside the Titan's
    bow inside the lathe's inflation.
Usage: python3 -I verification/results/lathe-bubble/lathe_shadow_rows.py [fl69.log] [fl68.log] [screenshot.png]"""
import json
import re
import sys
from pathlib import Path

R = Path(__file__).resolve().parents[3]
d = json.loads((R / 'verification/results/native-windows/weapon-effects/summary.json').read_text())
m = d['metrics']
print('record passed', d['passed'], 'checks', len(d['checks']), 'failed', d['failed_checks'])
for x in m['lathe']:
    if x.get('case') in ('flight_spread', 'flight_64_hits', 'bright_hull', 'bright_hull_taa', 'past_life', 'band',
                         'exact_zero'):
        print('LATHE', ' '.join(f'{k}={v}' for k, v in x.items()))
for x in m['timing']:
    if x['case'].startswith('titan_'):
        print(f'TIMING {x["width"]}x{x["height"]} {x["case"]} draws={x["patches"]} gpu_us={x["gpu_us"]} '
              f'raster_share={x["raster_share"]} lit_share={x["lit_share"]}')
logs = sys.argv[1:3] or ['/mnt/c/X3FL/x3m.log', '/mnt/c/X3FL/x3m.prev.log']
quoted = {('7', '13'), ('2', '9'), ('71', '16')}
for path in logs:
    if not Path(path).exists():
        print('LOG missing', path)
        continue
    hdr, frames, lathe_field = {}, [], False
    with open(path, errors='replace') as fh:
        for line in fh:
            if line.startswith('hdr_frame '):
                f = dict(re.findall(r'(\w+)=([^ ]+)', line))
                try:
                    hdr[int(f['frame'])] = (float(f['ev']), float(f['luma_p99']), float(f['ev_limit']))
                except (KeyError, ValueError):
                    pass
            elif line.startswith('weapon_effects_frame '):
                f = dict(re.findall(r'(\w+)=([^ ]+)', line))
                lathe_field = lathe_field or 'lathes' in f
                frames.append(f)
    evs = [v for v in hdr.values()]
    # the sun-shadow caster candidates around the Titan's hits (the follow-up's hypothesis)
    with open(path, errors='replace') as fh:
        for line in fh:
            if line.startswith('shadow_replay_candidates device'):
                f = dict(re.findall(r'(\w+)=([^ ]+)', line))
                if f.get('frame') in ('92700', '93000', '93300', '93600', '93900', '94200', '96300'):
                    print(f'  CASTERS frame={f["frame"]} routed={f["routed"]} c2={f["c2"]} c3={f["c3"]}')
    print(f'LOG {path} lathe_counters={lathe_field} hdr_rows={len(evs)} p99_at_clip={sum(p >= 64 for _, p, _ in evs)} '
          f'ev_le_-1.5={sum(e <= -1.5 for e, _, _ in evs)} ev_min={min(e for e, _, _ in evs):.3f} '
          f'ev_at_rest_1.3={sum(abs(e - 1.3) < 1e-3 for e, _, _ in evs)}')
    for f in frames:
        if (f.get('hits'), f.get('live')) in quoted:
            k = min(hdr, key=lambda n: abs(n - int(f['frame'])))
            print(f'  QUOTED frame={f["frame"]} hits={f["hits"]} live={f["live"]} lathes={f.get("lathes")} '
                  f'ev={hdr[k][0]:.3f} ev_limit={hdr[k][2]:.3f} p99={hdr[k][1]:.1f}')
shot = sys.argv[3] if len(sys.argv) > 3 else None
if shot and Path(shot).exists():
    import numpy as np
    from PIL import Image
    L = np.asarray(Image.open(shot).convert('RGB')).astype(float).mean(axis=2)
    for name, (x0, y0, x1, y1) in {'honeycomb': (600, 250, 850, 450), 'titan_no_cells': (1100, 300, 1350, 450),
                                   'station_sunlit': (50, 200, 350, 450), 'open_sky': (1400, 700, 1600, 900),
                                   'sky_beside_bow': (1490, 470, 1560, 590)}.items():
        p = np.percentile(L[y0:y1, x0:x1], [10, 50, 90, 99])
        print(f'SHOT {name} p10={p[0]:.1f} p50={p[1]:.1f} p90={p[2]:.1f} p99={p[3]:.1f}')
