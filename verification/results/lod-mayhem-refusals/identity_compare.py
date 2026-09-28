"""Byte identity of the overlay before and after the 2026-09-29 trailing-bytes rule (MAX_TRAILING 8 -> None)
on bodies that already baked: identity_sample.txt (3 bodies with 1-2 tolerated stray bytes, 2 clean).

Produced with (OLD = `git archive HEAD tools/analysis` of the pre-change commit, extracted to a scratch dir):
  PYTHONPATH=verification/probe:$OLD/tools/analysis python3 $OLD/tools/analysis/lod_overlay.py --batch --jobs 2 \
      --only verification/results/lod-mayhem-refusals/identity_sample.txt --out $SCRATCH/idold
  PYTHONPATH=verification/probe:tools/analysis python3 tools/analysis/lod_overlay.py --batch --jobs 2 \
      --only verification/results/lod-mayhem-refusals/identity_sample.txt --out $SCRATCH/idnew
  PYTHONPATH=verification/probe:tools/analysis python3 verification/results/lod-mayhem-refusals/identity_compare.py \
      $SCRATCH/idold $SCRATCH/idnew
"""
import hashlib
import json
import sys
from pathlib import Path

from inspect_x3 import read_catalogue


def members(root):
    cat = next(Path(root, 'addon').glob('*.cat'))
    raw = bytes(v ^ 0x33 for v in cat.with_suffix('.dat').read_bytes())
    return cat, {e['path']: raw[e['offset']:e['offset'] + e['size']] for e in read_catalogue(cat)}


def main(old, new):
    (oc, om), (nc, nm) = members(old), members(new)
    same = sum(1 for p in om if nm.get(p) == om[p])
    print(f'members old {len(om)} new {len(nm)} identical {same}; cat identical {oc.read_bytes() == nc.read_bytes()};'
          f' dat identical {oc.with_suffix(".dat").read_bytes() == nc.with_suffix(".dat").read_bytes()}')
    trailing = {b['name']: b.get('trailing') for b in json.loads(Path(new, 'x3m-lod-batch.json').read_text())['bodies']}
    print('trailing per body', trailing)
    print('dat sha256', hashlib.sha256(nc.with_suffix('.dat').read_bytes()).hexdigest()[:16])
    return 0 if same == len(om) == len(nm) else 1


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:3]))
