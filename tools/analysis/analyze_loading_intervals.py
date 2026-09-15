#!/usr/bin/env python3
"""Reduce a bounded frozen loading-interval artifact; durations are QPC ticks.

Unions measure retained wrapper occupancy, including blocked time. They do not
identify the critical path or a causal residual. Present TID is not main-thread
proof. Binary records and successful dumps remain local and untracked.
"""
import argparse
import json
import struct
from pathlib import Path

HEADER = struct.Struct('<8s8I7Q')
THREAD = struct.Struct('<4I2Q2IQ')
RECORD = struct.Struct('<QQII')


def merge(rows):
    result = []
    for begin, end in sorted(rows):
        if result and begin <= result[-1][1]:
            result[-1][1] = max(end, result[-1][1])
        else:
            result.append([begin, end])
    return result


def reduce(rows, begin, end):
    union = merge((max(a, begin), min(b, end)) for a, b in rows if a < end and b > begin)
    gaps, cursor = [], begin
    for a, b in union:
        if a > cursor:
            gaps.append([cursor, a])
        cursor = b
    if cursor < end:
        gaps.append([cursor, end])
    return dict(union_ticks=sum(b-a for a, b in union),
                largest_gaps=sorted(gaps, key=lambda row: row[1]-row[0], reverse=True)[:8]), union


def analyze(path):
    with Path(path).open('rb') as source:
        def get(fmt):
            data = source.read(fmt.size)
            if len(data) != fmt.size:
                raise ValueError('truncated artifact')
            return fmt.unpack(data)
        (magic, schema, header_bytes, record_bytes, slots, capacity, flags,
         registered, present_tid, frequency, initialized, begin, end,
         device, reset, frame) = get(HEADER)
        if (magic, schema, header_bytes, record_bytes, slots, capacity) != (b'X3MINT01', 1, 96, 24, 16, 65536):
            raise ValueError('unsupported schema or capacity')
        if flags & ~31 or registered > slots or not 0 < initialized <= begin < end or not frequency:
            raise ValueError('invalid marker, clock or registry identity')
        all_rows, threads, generations, total = [], [], set(), 0
        for index in range(slots):
            tid, generation, count, completed, lost_begin, lost_end, faults, reserved, last_end = get(THREAD)
            if (count != min(completed,capacity) or reserved or faults & ~31 or
                    bool(generation) != (index < registered) or
                    (generation and (generation != index+1 or not tid)) or
                    (not generation and (tid or count or completed or faults))):
                raise ValueError('invalid thread metadata')
            if bool(lost_begin) != bool(lost_end) or lost_begin > lost_end or lost_end > last_end:
                raise ValueError('invalid loss envelope')
            if completed > capacity and not lost_begin:
                raise ValueError('missing retention loss envelope')
            if generation in generations:
                raise ValueError('reused generation')
            if generation:
                generations.add(generation)
            rows = []
            for _ in range(count):
                a, b, operation, reserved = get(RECORD)
                if not a or b < a or b > last_end or operation >= 44 or reserved:
                    raise ValueError('invalid record/clock/operation')
                rows.append((a, b))
            total += count
            if not generation:
                continue
            intersects_loss = bool(lost_begin and lost_begin < end and lost_end > begin)
            coverage, union = reduce(rows, begin, end)
            coverage.update(tid=tid, generation=generation, records=count,
                            completed=completed, overwritten=completed-count,
                            lost_envelope=[lost_begin, lost_end],
                            flags=faults, complete=not(flags or faults or intersects_loss),
                            presenting_tid_match=tid == present_tid)
            threads.append(coverage)
            all_rows.extend(union)
        if source.read(1):
            raise ValueError('trailing data')
    any_thread, _ = reduce(all_rows, begin, end)
    complete = not flags and all(t['complete'] for t in threads)
    return dict(schema=1, frequency=frequency, initialized_qpc=initialized,
                begin_qpc=begin, end_qpc=end, duration_ticks=end-begin,
                device=device, reset=reset, frame=frame, present_tid=present_tid,
                flags=flags, retained_records=total, complete=complete,
                threads=threads, any_thread=any_thread,
                gaps_meaning=('time with no retained admitted hooked activity' if complete else
                              'gaps in incomplete retained evidence'),
                limits='Wrapper occupancy includes waits; no critical-path or main-thread identity proof.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.artifact), indent=2))


if __name__ == '__main__':
    main()
