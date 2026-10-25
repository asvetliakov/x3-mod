#!/usr/bin/env python3
"""Print the body names held by the overlay catalogues addon/13..15 (read-only). Usage: overlay_bodies.py GAME"""
import sys
sys.path.insert(0, 'tools/analysis')
from pathlib import Path
import bob1
assets = bob1._archive_modules().Assets(Path(sys.argv[1]))
names = set()
for k, es in assets.entries.items():
    for e in es:
        if str(e.get('source', '')).replace('\\', '/').lower() in ('addon/13.cat', 'addon/14.cat', 'addon/15.cat') \
                and k.endswith(('.pbb', '.bob')):
            names.add(k.removeprefix('addon/').removeprefix('objects/').rsplit('.', 1)[0])
print('\n'.join(sorted(names)))
