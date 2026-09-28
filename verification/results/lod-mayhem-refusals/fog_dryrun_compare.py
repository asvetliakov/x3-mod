"""fog_families output for the install before and after bob1.parse(data, None) in fog_families.body_materials.

Each side (before: the unchanged tool; after: the changed tool), game tree read-only:
  PYTHONPATH=verification/probe:tools/analysis python3 tools/analysis/fog_families.py --dry-run --game GAME > SIDE.txt
  PYTHONPATH=verification/probe:tools/analysis python3 tools/analysis/fog_families.py --check --game GAME > SIDE_check.txt
Then: python3 verification/results/lod-mayhem-refusals/fog_dryrun_compare.py BEFORE.txt AFTER.txt BEFORE_check.txt AFTER_check.txt
The dry run's family rows (every line but the final JSON counts line) must match exactly; the counts line
must match without wall_seconds; the --check lines must match exactly.
"""
import hashlib
import json
import sys


def split(path):
    lines = open(path).read().splitlines()
    counts = json.loads(lines[-1])
    counts.pop('wall_seconds', None)
    return lines[:-1], counts


def main(before, after, check_before, check_after):
    (rb, cb), (ra, ca) = split(before), split(after)
    same_rows = rb == ra
    print(f'family rows before {len(rb)} after {len(ra)} identical {same_rows}'
          f' sha256 {hashlib.sha256(chr(10).join(ra).encode()).hexdigest()[:16]}')
    print(f'counts identical {cb == ca}: {ca}')
    kb, ka = open(check_before).read(), open(check_after).read()
    print(f'--check identical {kb == ka}: {ka.strip().splitlines()[-1][-60:]}')
    return 0 if same_rows and cb == ca and kb == ka else 1


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:5]))
