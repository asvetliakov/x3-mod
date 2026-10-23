#!/usr/bin/env python3
"""Decoded KC listings behind docs/reverse-engineering/music-script-driver.md (written untracked under raw/).

Reads L/x3story.obj in memory from the newest catalogue that carries it on this install (addon/07.cat, the Mayhem
object the game loads; addon/04.cat is stock AP 3.3 for comparison) and writes, per build, under raw/:
  <build>_music_methods.txt   linear + tracked listings of the class-200 music methods, 150::NotifyAlert/IsAlert,
                              402::EnterSector/WarpEnterSector and 200::ChangeTrackToCombatMusic
  <build>_mov_sites.txt       every MOV_* native call site with its containing method
  <build>_music_callers.txt   every call of the music entry points (by CODE entry or by name)
Decoded instructions only (no script bytes). raw/ is git-ignored: keep it local.

    python3 -I verification/results/music-script-driver/music_driver_listing.py [X3 dir]
"""
import bisect, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'ship-navigation-ai' / 'gate-queue'))
from kc_obj import Obj, load_member  # noqa: E402

X3 = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/mnt/c/X3')
BUILDS = [('mayhem', X3 / 'addon/07.cat'), ('stock', X3 / 'addon/04.cat')]
METHODS = [(200, n) for n in ('MusicFadeOut', 'MusicFadeIn', 'PlayMusicWithFade', 'LoopMusic', 'StartMusic', 'StopMusic',
                              'PlayMusicToEnd', 'ChangeTrackMusic', 'ChangeTrackToSectorMusic', 'ChangeTrackToCombatMusic',
                              'ChangeTrackMusicDelayed', 'WaitForEndOfTrack', 'EnvironmentToMusicTrack')] + [
    (150, 'NotifyAlert'), (150, 'IsAlert'), (402, 'EnterSector'), (402, 'WarpEnterSector'),
    (3001, 'GetEnvironmentTrack'), (3100, 'GetEnvironmentTrack'), (2001, 'GetMusicTrack'), (2004, 'IsAttacked'),
    (2004, 'GetAttacker'), (2004, 'SetAttacker')]
ENTRY_NAMES = {'ChangeTrackToSectorMusic', 'ChangeTrackToCombatMusic', 'ChangeTrackMusic', 'LoopMusic', 'StartMusic',
               'StopMusic', 'WaitForEndOfTrack', 'PlayMusicToEnd', 'ChangeTrackMusicDelayed', 'PlayMusicWithFade',
               'NotifyAlert', 'IsAlert', 'EnvironmentToMusicTrack', 'GetEnvironmentTrack', 'SetMusicTrack',
               'GetMusicTrack'}


def owner(o, pc):
    e = o.entries[bisect.bisect_right(o.entries, pc) - 1]
    return '%05x %s' % (e, ' '.join('%d::%s' % m[:2] for m in o.meth[e]))


out_dir = HERE / 'raw'
out_dir.mkdir(exist_ok=True)
for label, cat in BUILDS:
    o = Obj(load_member(cat))
    with open(out_dir / ('%s_music_methods.txt' % label), 'w') as f:
        for cid, nm in METHODS:
            for e in o.find(cid, nm):
                f.write('== %d::%s entry %05x\n' % (cid, nm, e))
                for pc in o.insns(e):
                    f.write('  ' + o.line(pc) + '\n')
                lines, bad = o.track(e)
                f.write('  -- tracked (depth conflicts %d)\n' % bad)
                for pc, d, ln in lines:
                    f.write('  %05x d%-3d %s\n' % (pc, d, ln))
    with open(out_dir / ('%s_mov_sites.txt' % label), 'w') as f, \
            open(out_dir / ('%s_music_callers.txt' % label), 'w') as g:
        for pc in o.all_insns():
            op = o.code[pc]
            if op not in (0x82, 0x84, 0x85, 0x86, 0x87, 0x88):
                continue
            nm = o.operand(pc)
            if op == 0x82 and nm.startswith('MOV_'):
                f.write('%05x %-26s in %s\n' % (pc, nm, owner(o, pc)))
            if nm.split('::')[-1] in ENTRY_NAMES:
                g.write('%05x %02x %-34s in %s\n' % (pc, op, nm, owner(o, pc)))
    print('%s: wrote %s' % (label, ', '.join(sorted(p.name for p in out_dir.glob(label + '_*')))))
