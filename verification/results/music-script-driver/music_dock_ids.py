#!/usr/bin/env python3
"""Docking music ids and the undock request, both story builds (music-script-driver.md section 10).

Reads L/x3story.obj from the catalogues the game loads (addon/07.cat Mayhem, addon/04.cat stock AP) and prints the
decoded (tracked) statements of the methods that choose a docking or landing track, plus every music-entry call site
whose decoded statement carries the literal 14 or 2001..2004. Decoded statements only (no script bytes).

    python3 -I verification/results/music-script-driver/music_dock_ids.py [X3 dir]
"""
import re, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
from kc_obj import Obj, load_member  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
BUILDS = [('mayhem', X3 / 'addon/07.cat'), ('stock', X3 / 'addon/04.cat')]
METHODS = [(150, 'DockingAllowed'), (200, 'EnvironmentToMusicTrack'), (200, 'ChangeTrackToSectorMusic')]
ENTRIES = ('ChangeTrackMusic', 'LoopMusic', 'StartMusic', 'PlayMusicWithFade', 'ChangeTrackToSectorMusic')
DOCK = re.compile(r'\b(14|200[1-4])\b')

for label, cat in BUILDS:
    o = Obj(load_member(cat))
    print('#### build', label)
    for cid, nm in METHODS:
        for e in o.find(cid, nm):
            lines, bad = o.track(e)
            print('== %d::%s entry %05x (depth conflicts %d)' % (cid, nm, e, bad))
            for pc, d, ln in lines:
                if 'call' in ln or 'return' in ln or 'Random' in ln or DOCK.search(ln):
                    print('  %05x %s' % (pc, ln))
    # Every decoded call of a music entry whose statement carries a dock literal, and every caller of
    # ChangeTrackToSectorMusic (the docked and the undocked requests both go through it).
    hits, sector_callers = [], set()
    for e in o.entries:
        try:
            lines, _ = o.track(e)
        except Exception:
            continue
        meth = ' '.join('%d::%s' % m[:2] for m in o.meth[e])
        for pc, d, ln in lines:
            if any(('::%s(' % n) in ln for n in ENTRIES):
                if DOCK.search(ln) and 'ChangeTrackToSectorMusic' not in ln:
                    hits.append('  %05x %-40s %s' % (pc, meth, ln))
                if '::ChangeTrackToSectorMusic(' in ln:
                    sector_callers.add(meth)
    print('-- music-entry calls with a dock literal (14, 2001..2004): %d' % len(hits))
    print('\n'.join(sorted(set(hits))))
    print('-- callers of ChangeTrackToSectorMusic: %d' % len(sector_callers))
    print('  ' + ', '.join(sorted(sector_callers)))
