"""Per-latch HDR meter readback cost from one fixture session log (the hdr_frame rows).

usage: python3 readback_timing.py LABEL SESSION_LOG

Same reduction as ../hdr-readback-double-buffer/readback_timing.py (n/median/p95/max over the latches that locked or
copied: readback != 00000001), plus the lock gate's fields when present: meter_event_ready over every row
(0 = skipped, 1 = locked after S_OK, 2 = capped lock, -1 = no lock attempted / no query), the poll-only cost of the
skipped latches (readback_lock_us there is the GetData poll alone) and the session counters of the last row.
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
    out = [f'{label}: frames={len(rows)} readback_latches={len(work)} stepped={sum(r.get("stepped") == "1" for r in rows)} '
           f'readback_failures={sum(r["readback"] not in ("00000000", "00000001") for r in work)}']
    for key in ('readback_copy_us', 'readback_lock_us', 'readback_us'):
        if rows and key in rows[0]:
            out.append(f'  {key}: {describe([float(r[key]) for r in work])}')
    if rows and 'meter_event_ready' in rows[0]:
        counts = {}
        for r in rows:
            counts[r['meter_event_ready']] = counts.get(r['meter_event_ready'], 0) + 1
        out.append(f'  meter_event_ready over all rows: {dict(sorted(counts.items()))}')
    if rows and 'meter_skips' in rows[0]:
        skipped = [r for r in rows if r['meter_event_ready'] == '0']
        out.append(f'  skipped latches readback_lock_us (poll only): {describe([float(r["readback_lock_us"]) for r in skipped])}')
        last = rows[-1]
        out.append('  session: ' + ' '.join(f'{k}={last[k]}' for k in ('meter_skip_total', 'meter_poll_errors', 'meter_cap_locks')))
    print('\n'.join(out))


if __name__ == '__main__':
    main()
