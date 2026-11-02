"""Atlas merged materials (non-widened synth rows with atlas=True) per body named in ONLY, in each marker given.
Usage: merged_counts.py ONLY.txt LABEL=MARKER.json[,MARKER.json] ..."""
import json
import sys

names = [n.strip() for n in open(sys.argv[1]) if n.strip()]
for arg in sys.argv[2:]:
    label, paths = arg.split('=', 1)
    rows = {}
    for path in paths.split(','):
        for b in json.load(open(path))['bodies']:
            if b['name'] in names:
                rows[b['name']] = (sum(1 for s in b.get('synth', []) if s.get('atlas') and not s.get('widened')),
                                   b.get('draws'))
    for n in names:
        print(label, n, 'merged=%s draws=%s' % rows.get(n, (None, None)))
