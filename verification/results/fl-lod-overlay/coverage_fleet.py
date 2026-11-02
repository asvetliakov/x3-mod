#!/usr/bin/env python3
"""Fleet-wide bake coverage, FL batch (OUT/x3m-lod-batch.json) against the installed AP batch
(C:\\X3\\addon\\x3m-lod-batch.json, the Mayhem-modded AP view; read only), by body key (lower-case name):
- in both views, baked by AP and not by FL, with the FL reasons (and the AP member, to tell a mod body);
- in both views, baked by FL and not by AP, with the AP reasons;
- counts of FL-only bodies by FL outcome and AP-only bodies by AP outcome;
- the FL reasons over all FL refusals, split into reason classes. Counts and names only.
usage: coverage_fleet.py OUT"""
import json
import sys
from collections import Counter
from pathlib import Path

fl = {b['name'].lower(): b for b in json.loads((Path(sys.argv[1]) / 'x3m-lod-batch.json').read_text())['bodies']}
ap = {b['name'].lower(): b for b in json.loads(Path('/mnt/c/X3/addon/x3m-lod-batch.json').read_text())['bodies']}


def why(b):
    return ','.join(b['refuse'] + b['filter']) or 'eligible'


def outcome(b):
    return 'baked' if b['eligible'] else (b['refuse'] + b['filter'])[0]


both = sorted(fl.keys() & ap.keys())
ap_not_fl = [k for k in both if ap[k]['eligible'] and not fl[k]['eligible']]
fl_not_ap = [k for k in both if fl[k]['eligible'] and not ap[k]['eligible']]
print(f'FL bodies {len(fl)}, AP bodies {len(ap)}, in both {len(both)}; baked in both'
      f' {sum(1 for k in both if fl[k]["eligible"] and ap[k]["eligible"])}')
print(f'\n== baked by AP, not by FL ({len(ap_not_fl)}): body | category | FL reasons | FL member | AP member')
for k in ap_not_fl:
    print(f'{fl[k]["name"]} | {fl[k]["cat"]} | {why(fl[k])} | {fl[k]["member"]} | {ap[k]["member"]}')
print('by FL reason', Counter(r for k in ap_not_fl for r in fl[k]['refuse'] + fl[k]['filter']).most_common())
print('AP member from a mod catalogue (addon/05..12)',
      sum(1 for k in ap_not_fl if any(f'addon/{n:02d}.cat' in ap[k]['member'] for n in range(5, 13))))
print(f'\n== baked by FL, not by AP ({len(fl_not_ap)}): body | category | AP reasons | FL member | AP member')
for k in fl_not_ap:
    print(f'{fl[k]["name"]} | {fl[k]["cat"]} | {why(ap[k])} | {fl[k]["member"]} | {ap[k]["member"]}')
print('by AP reason', Counter(r for k in fl_not_ap for r in ap[k]['refuse'] + ap[k]['filter']).most_common())
fl_only = sorted(fl.keys() - ap.keys())
ap_only = sorted(ap.keys() - fl.keys())
print(f'\n== FL-only bodies {len(fl_only)} by FL outcome', Counter(outcome(fl[k]) for k in fl_only).most_common())
print('FL-only baked:', ', '.join(fl[k]['name'] for k in fl_only if fl[k]['eligible']) or '-')
print('FL-only ships/stations not baked:', '; '.join(f'{fl[k]["name"]} ({why(fl[k])})' for k in fl_only
                                                     if not fl[k]['eligible'] and fl[k]['cat'] != 'other') or '-')
print(f'== AP-only bodies {len(ap_only)} by AP outcome', Counter(outcome(ap[k]) for k in ap_only).most_common())
ships = [b for b in fl.values() if b['cat'] in ('ship', 'station') and not b['eligible']]
print('\n== FL ship/station not baked, by first reason', Counter((b['refuse'] + b['filter'])[0] for b in ships).most_common())
