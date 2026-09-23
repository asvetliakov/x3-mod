"""Review item D: batch_stem_collision is counted only among otherwise eligible bodies (atlas
texture names use the file stem only). Lists the colliding groups.
Usage: python3 stem_collisions.py [census.txt]"""
import re
import sys

rows = open(sys.argv[1] if len(sys.argv) > 1 else 'census.txt').read().splitlines()
hit = [r for r in rows if 'batch_stem_collision' in r]
bad = [r for r in hit if not re.search(r' refuse=batch_stem_collision filter=-( |$)', r)]
print(f'batch_stem_collision {len(hit)}; with any other refusal or filter {len(bad)} (must be 0)')
groups = {}
for r in hit:
    groups.setdefault(r.split()[0].rsplit('/', 1)[-1].lower(), []).append(r.split()[0])
for stem, names in sorted(groups.items()):
    print(f'  {stem}: {", ".join(names)}')
