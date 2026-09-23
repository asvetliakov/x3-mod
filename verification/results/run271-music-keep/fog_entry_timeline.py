"""Run 72 B: fog timeline per sector entry. Usage: fog_entry_timeline.py <session.log>
For every volumetric_fog_sector row: frame, reason/profile, and from the following frames the first frame of each
volumetric_fog_frame reason, the first cards ready / suppressed>0 / applied frame, the far/fine ready events,
with elapsed seconds from frame_end qpc. Also prints loading_phase rows and runs of fog reason per frame."""
import re, sys
kv = re.compile(r'(\w+)=(\S+)')
fe, sectors, frames, cards, cache, loading = {}, [], {}, {}, [], []
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        k = line.split(' ', 1)[0]
        if k not in ('frame_end', 'volumetric_fog_sector', 'volumetric_fog_frame', 'volumetric_fog_cards', 'volumetric_fog_cache', 'loading_phase', 'volumetric_fog_prepare'):
            continue
        d = dict(kv.findall(line)); fr = int(d.get('frame', -1))
        if k == 'frame_end': fe[fr] = int(d['qpc'])
        elif k == 'volumetric_fog_sector': sectors.append(d)
        elif k == 'volumetric_fog_frame': frames[fr] = (d['applied'], d['reason'])
        elif k == 'volumetric_fog_cards': cards[fr] = (d['ready'], d['warmup'], d['suppressed'], d['observed'], d['refused'], d['reason'])
        elif k == 'volumetric_fog_cache': cache.append((fr, d.get('event'), d.get('ms'), d.get('reason')))
        elif k == 'loading_phase': loading.append((d['name'], fr, d.get('stall_ms')))
def t(fr):
    return fe.get(fr)
print('loading_phase', loading)
for i, s in enumerate(sectors):
    f0 = int(s['frame']); f1 = int(sectors[i + 1]['frame']) if i + 1 < len(sectors) else max(fe)
    out = [f"sector frame={f0} reason={s['reason']} profile={s['profile']} sector={s['sector']}"]
    # runs of fog frame reason
    runs, prev = [], None
    for fr in range(f0, f1):
        r = frames.get(fr)
        key = (r[0], r[1]) if r else ('-', 'no_row')
        if key != prev: runs.append([fr, fr, key]); prev = key
        else: runs[-1][1] = fr
    firsts = {}
    for fr in range(f0, f1):
        c = cards.get(fr)
        if not c: continue
        if c[0] == '1' and 'ready' not in firsts: firsts['ready'] = fr
        if int(c[2]) > 0 and 'suppressed' not in firsts: firsts['suppressed'] = fr
        if int(c[3]) > 0 and 'observed' not in firsts: firsts['observed'] = fr
    def el(fr):
        return None if fr is None or t(fr) is None or t(f0) is None else round((t(fr) - t(f0)) / 1e7, 2)
    out.append('  runs: ' + '; '.join(f"{a}-{b} applied={k[0]} {k[1]} (+{el(a)}s..+{el(b)}s)" for a, b, k in runs[:12]) + (f' ... {len(runs)} runs' if len(runs) > 12 else ''))
    out.append('  cards first: ' + ', '.join(f"{k}={v} (+{el(v)}s)" for k, v in firsts.items()))
    out.append('  cache: ' + ', '.join(f"{e}@{fr}(+{el(fr)}s ms={ms} {r})" for fr, e, ms, r in cache if f0 <= fr < f1))
    print('\n'.join(out))
