#!/usr/bin/env python3
"""Probe lengths of the object-lifetime registry table (src/proxy/object_lifetime.cpp find()).

Replays find()'s exact placement: home = (key * 2654435761) mod 2^32 mod capacity,
linear probing, insert into the first empty slot (fresh table, no tombstones).
Reports, at 50,000 live keys, the longest successful probe (slots read to find
a present key) and the longest unsuccessful probe (slots read up to and including
the empty slot that ends the search, over every home slot).
Output: probe_length_out.txt beside this script (python3 probe_length.py > probe_length_out.txt).
"""
import random


def home(key, capacity):
    return (key * 2654435761 & 0xffffffff) % capacity


def probes(capacity, keys):
    slots = [0] * capacity
    for k in keys:
        i = home(k, capacity)
        while slots[i]:
            i = (i + 1) % capacity
        slots[i] = k
    hit = 0
    for k in keys:
        i, n = home(k, capacity), 1
        while slots[i] != k:
            i, n = (i + 1) % capacity, n + 1
        hit = max(hit, n)
    run = best = 0
    for s in slots + slots:  # longest occupied run, wrapping
        run = run + 1 if s else 0
        best = max(best, run)
    return hit, min(best, capacity) + 1


def main():
    random.seed(20260929)
    live = 50000
    cases = {
        'sequential handles 1..50000': list(range(1, live + 1)),
        '50000 of handles 1..200000': random.sample(range(1, 200001), live),
        '50000 uniform 32-bit keys': random.sample(range(1, 1 << 32), live),
        # Backward-shift deletion leaves exactly the fresh-table placement of the live keys, so a window of
        # live handles after long churn is this case: 1000 old survivors plus the newest 49000 handles.
        'churned: 1..1000 plus 313001..362000': list(range(1, 1001)) + list(range(313001, 362001)),
    }
    for capacity in (65536, 131072, 262144):
        for name, keys in cases.items():
            hit, miss = probes(capacity, keys)
            print(f'capacity={capacity} load={live / capacity:.3f} case="{name}" '
                  f'max_hit_probe={hit} max_miss_probe={miss}')


if __name__ == '__main__':
    main()
