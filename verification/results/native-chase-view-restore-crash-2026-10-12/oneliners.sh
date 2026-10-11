#!/bin/sh
# Run147 native crash triage (2026-10-12). Inputs are local copies; outputs beside this script.
S=${S:-/tmp/claude-1000/-home-asvetl-x3-mod/e54ef5fd-8036-45b2-b638-60c6a2d87dd4/scratchpad}
R=$(dirname "$0"); A=$R/../native-chase-view-restore-2026-10-12; EXE=${EXE:-/mnt/c/X3/X3AP.exe}
python3 -I $R/../windows-crash-2026-10-11/parse_minidump.py $S/X3Crash-run147-sector.dmp 4d4d5544 | grep -v '^   '
python3 -I $R/stack_walk.py $S/X3Crash-run147-sector.dmp 60
# our 7 cdecl args above the 0x49f570 frame (ret into d3d9 at 0x001af880); 0x4a2260 saved regs/args; VM table slot 0..1
python3 -I $R/read_words.py $S/X3Crash-run147-sector.dmp 001af880:8 001af7a0:8 04ea5d88:12
for r in 0049f570:0049f676 0049f330:0049f422 0049f430:0049f4b6 004a2260:004a2321 004ab170:004ab19b 004ab0a0:004ab0fe 004a43fe:004a4428; do
  echo "== $r"; python3 -I $A/disasm.py $EXE ${r%:*} ${r#*:}; done
python3 -I $A/callers.py $EXE 0049f570 004a2260 004ab170
python3 -I $R/entries_near.py $S/x3story_07.obj 106a00 107000
grep -n 'ui_scale_camera\|volumetric_fog_sector\|chase_view_restore\|^exception' $S/x3m-run147-sector.log $S/x3m-run147-ship.log | cut -c1-160
# d3d9 return addresses -> symbols (installed Run147 DLL, 8eb20091; preferred base 0x6fb40000, loaded at 0x6e040000)
i686-w64-mingw32-nm -C /mnt/c/X3/rollback-d3d9/d3d9.dll.run147 | python3 -I -c "
import sys,bisect
r=sorted((int(p[0],16),p[2].strip()) for p in (l.split(None,2) for l in sys.stdin) if len(p)==3 and p[1] in 'tT'); k=[x[0] for x in r]
for v in (0x677e4,0x6dffd,0x6effe):
    a=0x6fb40000+v; s=r[bisect.bisect_right(k,a)-1]; print(hex(v),'+'+hex(a-s[0]),s[1][:90])"
