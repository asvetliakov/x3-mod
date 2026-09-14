#!/usr/bin/env python3
"""Scan the startup replica's decoded-PCM dump for the artefacts that sound like crackle.

Input is the raw file written by `voice_startup_replica.exe --dump-pcm` plus the
stdout of that run, which carries the negotiated format (`REPLICA_PCM_FORMAT`)
and one `REPLICA_PCM` row per consumed application sample (byte count, running
byte offset and the buffer's stream timestamps). Nothing here decodes anything:
it only measures what the game's own consumer would have received.

Artefact classes, all reported with the first ten timestamps:

- **jumps**: adjacent-sample steps above `--jump-fraction` of full scale, split
  into the ones inside the first `--boundary-ms` of a buffer (a decoder/handover
  seam) and the ones in buffer interiors (a decode error inside one buffer);
- **dropouts**: runs of exact zeros or of one constant value (DC) lasting at
  least `--dc-ms`, surrounded by non-silent audio;
- **timestamp gaps/overlaps**: `t_start` of a buffer that does not meet the
  previous `t_end` within one sample period.

Importable: `parse_log`, `read_pcm`, `scan`.
"""
import argparse
import array
import json
import math
import re
import sys
from pathlib import Path

FULL_SCALE = 32768  # int16 peak
HUNDRED_NS = 1e7
SILENCE_FLOOR = 64  # |sample| at or below this counts as silence (~-54 dBFS)


def _fields(line):
    return dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)


def parse_log(text):
    """Negotiated format and per-buffer rows from a replica stdout (or its tail)."""
    fmt = None
    buffers = []
    for line in text.splitlines():
        if line.startswith('REPLICA_PCM_FORMAT '):
            f = _fields(line)
            fmt = dict(tag=int(f['tag']), channels=int(f['ch']), rate=int(f['rate']), bits=int(f['bits']))
        elif line.startswith('REPLICA_PCM '):
            f = _fields(line)
            buffers.append(dict(cycle=int(f['cycle']), bytes=int(f['bytes']), offset=int(f['offset']),
                                t_start=int(f['t_start']) / HUNDRED_NS, t_end=int(f['t_end']) / HUNDRED_NS,
                                src=f.get('src', 'offset')))
    if fmt is None:
        raise ValueError('no REPLICA_PCM_FORMAT line: the run did not dump PCM')
    if fmt['bits'] != 16 or fmt['tag'] != 1:
        raise ValueError(f'unsupported dump format {fmt}')
    return dict(format=fmt, buffers=buffers)


def read_pcm(path, fmt):
    """Interleaved samples as int16; mono is required (the game negotiates one channel)."""
    if fmt['channels'] != 1:
        raise ValueError('only the mono negotiation is scanned')
    data = Path(path).read_bytes()
    samples = array.array('h')
    samples.frombytes(data[:len(data) - len(data) % 2])
    if sys.byteorder == 'big':
        samples.byteswap()
    return samples


def _runs_of_constant(samples, minimum):
    """(start, length, value) for every maximal run of one repeated value of at least `minimum`."""
    out = []
    n = len(samples)
    i = 0
    while i < n:
        j = i + 1
        value = samples[i]
        while j < n and samples[j] == value:
            j += 1
        if j - i >= minimum:
            out.append((i, j - i, value))
        i = j
    return out


def scan(samples, fmt, buffers, jump_fraction=0.30, boundary_ms=10.0, dc_ms=5.0, context_ms=200.0, limit=10):
    rate = fmt['rate']
    threshold = jump_fraction * FULL_SCALE
    boundary_len = max(1, int(round(rate * boundary_ms / 1000.0)))
    dc_len = max(2, int(round(rate * dc_ms / 1000.0)))
    context = max(1, int(round(rate * context_ms / 1000.0)))
    starts = sorted({b['offset'] // 2 for b in buffers if b['offset'] // 2 < len(samples)})

    def in_boundary(index):
        # The window [start, start+boundary_len) of the buffer that contains `index`.
        lo, hi = 0, len(starts)
        while lo < hi:
            mid = (lo + hi) // 2
            if starts[mid] <= index:
                lo = mid + 1
            else:
                hi = mid
        return lo > 0 and index - starts[lo - 1] < boundary_len

    boundary_jumps, interior_jumps = [], []
    peak = 0
    energy = 0.0
    previous = samples[0] if samples else 0
    if samples:
        peak = abs(previous)
        energy = float(previous) * previous
    for i in range(1, len(samples)):
        value = samples[i]
        step = value - previous
        if step > threshold or -step > threshold:
            (boundary_jumps if in_boundary(i) else interior_jumps).append((i, abs(step)))
        a = -value if value < 0 else value
        if a > peak:
            peak = a
        energy += float(value) * value
        previous = value

    dropouts = []
    for start, length, value in _runs_of_constant(samples, dc_len):
        before = samples[max(0, start - context):start]
        after = samples[start + length:start + length + context]
        loud = lambda block: any(s > SILENCE_FLOOR or s < -SILENCE_FLOOR for s in block)
        if loud(before) and loud(after):
            dropouts.append((start, length, int(value)))

    gaps, overlaps = [], []
    tolerance = 1.0 / rate
    for previous_buffer, current in zip(buffers, buffers[1:]):
        delta = current['t_start'] - previous_buffer['t_end']
        if delta > tolerance:
            gaps.append((current['offset'] // 2, delta))
        elif delta < -tolerance:
            overlaps.append((current['offset'] // 2, -delta))

    seconds = len(samples) / float(rate) if rate else 0.0
    def times(rows, extra=None):
        return [dict(t=round(index / float(rate), 6), **(extra(row) if extra else {})) for index, *row in rows[:limit]]

    return dict(
        format=dict(fmt), samples=len(samples), seconds=round(seconds, 6), buffers=len(buffers),
        bytes=sum(b['bytes'] for b in buffers),
        peak=peak, rms=round(math.sqrt(energy / len(samples)), 2) if samples else 0.0,
        jump_threshold=int(threshold), boundary_ms=boundary_ms, dc_ms=dc_ms,
        jumps_interior=len(interior_jumps), jumps_boundary=len(boundary_jumps),
        dropouts=len(dropouts), dropout_ms_total=round(sum(r[1] for r in dropouts) * 1000.0 / rate, 3),
        timestamp_gaps=len(gaps), timestamp_overlaps=len(overlaps),
        timestamp_gap_ms_total=round(sum(d for _, d in gaps) * 1000.0, 3),
        first=dict(
            jump_interior=times(interior_jumps, lambda r: dict(step=int(r[0]))),
            jump_boundary=times(boundary_jumps, lambda r: dict(step=int(r[0]))),
            dropout=times(dropouts, lambda r: dict(ms=round(r[0] * 1000.0 / rate, 3), value=r[1])),
            gap=times(gaps, lambda r: dict(ms=round(r[0] * 1000.0, 3))),
            overlap=times(overlaps, lambda r: dict(ms=round(r[0] * 1000.0, 3))),
        ),
    )


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--pcm', type=Path, required=True)
    ap.add_argument('--log', type=Path, required=True, help='replica stdout.txt of the same run')
    ap.add_argument('--json', type=Path, default=None)
    ap.add_argument('--jump-fraction', type=float, default=0.30)
    ap.add_argument('--boundary-ms', type=float, default=10.0)
    ap.add_argument('--dc-ms', type=float, default=5.0)
    a = ap.parse_args(argv)
    log = parse_log(a.log.read_text(errors='replace'))
    samples = read_pcm(a.pcm, log['format'])
    report = scan(samples, log['format'], log['buffers'], a.jump_fraction, a.boundary_ms, a.dc_ms)
    report['pcm'] = str(a.pcm)
    report['log'] = str(a.log)
    if a.json:
        a.json.parent.mkdir(parents=True, exist_ok=True)
        a.json.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
