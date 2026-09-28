#!/usr/bin/env python3
"""The fixed A/B subset for the scheduling comparison: the ten largest bodies by texture_pixels (the census
predictor), by installed member_bytes and by installed bake seconds, then bodies spread evenly by texture_pixels
up to --count; written one name per line (lod_overlay.py --only).

  python3 ab_subset.py --features SCRATCH/features.json --out subset.txt [--count 160]
"""
import argparse
import json
import os

ap = argparse.ArgumentParser()
ap.add_argument('--features', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--count', type=int, default=160)
a = ap.parse_args()
feats = {f['name']: f for f in json.load(open(a.features))}
game = os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3')
inst = {b['name']: b for b in json.load(open(game + '/addon/x3m-lod-batch.json'))['bodies']
        if b.get('seconds') is not None}
pick = []
for key in (lambda n: feats[n]['texture_pixels'] or 0, lambda n: (inst.get(n) or {}).get('member_bytes', 0),
            lambda n: (inst.get(n) or {}).get('seconds', 0)):
    pick += [n for n in sorted(feats, key=key, reverse=True)[:10] if n not in pick]
rest = sorted((n for n in feats if n not in pick), key=lambda n: (feats[n]['texture_pixels'] or 0, n))
need = a.count - len(pick)
pick += [rest[int((i + 0.5) * len(rest) / need)] for i in range(need)]
with open(a.out, 'w') as f:
    f.write('\n'.join(pick) + '\n')
print(f'{len(pick)} bodies ({len(set(pick))} distinct) -> {a.out}')
