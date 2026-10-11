"""List which .cat indexes name x3story.obj / KC script objects (X3 cat index: byte ^ (0xDB + i) & 0xFF). Usage: python3 cat_x3story.py <cat>..."""
import sys, os
for p in sys.argv[1:]:
    b = open(p, 'rb').read()
    t = bytes((c ^ ((0xDB + i) & 0xFF)) for i, c in enumerate(b)).decode('latin1')
    hits = [l for l in t.splitlines() if 'x3story' in l.lower() or l.lower().endswith('.obj')]
    if hits:
        print(p, os.path.getsize(p), os.path.getmtime(p), hits[:5])
