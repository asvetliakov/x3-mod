"""Per-frame HDR meter readback timings from one fixture session log (the hdr_frame rows).

usage: python3 readback_timing.py LABEL SESSION_LOG

Reads either row form: before 2026-10-08 the combined `readback_transfer_lock_us`
(GetRenderTargetData + LockRect in one latch); after it the double-buffered
`readback_copy_us` / `readback_lock_us` and `meter_event_ready`. Prints one line
per label: frames, latches that read back, step count, and n/median/p95/max of
each timing field over the latches that did any readback work.
"""
import statistics
import sys


def fields(line):
    return dict(item.split('=', 1) for item in line.split()[1:] if '=' in item)


def describe(values):
    if not values:
        return 'n=0'
    ordered = sorted(values)
    p95 = ordered[min(len(ordered) - 1, int(round(0.95 * (len(ordered) - 1))))]
    return f'n={len(ordered)} p50={statistics.median(ordered):.1f} p95={p95:.1f} max={ordered[-1]:.1f}'


def main():
    label, path = sys.argv[1], sys.argv[2]
    rows = [fields(l) for l in open(path, errors='replace') if l.startswith('hdr_frame ')]
    work = [r for r in rows if r.get('readback') not in (None, '00000001')]
    keys = [k for k in ('readback_transfer_lock_us', 'readback_copy_us', 'readback_lock_us', 'readback_us') if rows and k in rows[0]]
    out = [f'{label}: frames={len(rows)} redirected={sum(r.get("redirected") == "1" for r in rows)} readback_latches={len(work)} '
           f'stepped={sum(r.get("stepped") == "1" for r in rows)} readback_failures={sum(r["readback"] not in ("00000000", "00000001") for r in work)}']
    for key in keys:
        out.append(f'  {key}: {describe([float(r[key]) for r in work])}')
    if rows and 'meter_event_ready' in rows[0]:
        counts = {}
        for r in work:
            counts[r['meter_event_ready']] = counts.get(r['meter_event_ready'], 0) + 1
        out.append(f'  meter_event_ready over readback latches: {dict(sorted(counts.items()))}')
        locked = [r for r in work if r['meter_event_ready'] in ('0', '1')]
        for state in ('0', '1'):
            sel = [float(r['readback_lock_us']) for r in locked if r['meter_event_ready'] == state]
            out.append(f'  readback_lock_us when meter_event_ready={state}: {describe(sel)}')
    print('\n'.join(out))


if __name__ == '__main__':
    main()
