"""Attribution run (2026-11-05). Usage, from the checkout, with a detached worktree of the Wine record's commit:
  git worktree add --detach WT 0cf873a6b   (ribbons: add the Scene::frame BeginScene / EndScene bracket there)
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 <this script> WT > attribution_0cf873a6b.json
Builds the ribbons fixture from the worktree at the Wine record's commit (scene bracket only) and runs it natively."""
import json, sys
from pathlib import Path
WT = Path(sys.argv[1])
sys.path.insert(0, str(WT / 'verification/probe'))
import run_engine_ribbons as r  # the record commit's runner: build() and parse() from that tree
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'verification/probe'))
import native_host
b = r.build()
d3dx = native_host.windows_path(native_host.GAME_DIR / 'd3dx9_37.dll')
done, text = native_host.run_native(r.EXE, [d3dx], r.BUILD, 900, log=r.BUILD / 'fixture-native')
rep = r.parse(text)
out = {'commit': '0cf873a6b', 'warnings': b['warnings'], 'exit': done.returncode, 'checks': rep['check_count'], 'failed': rep['failed_checks'],
       'resolve': [{k: x[k] for k in ('width', 'sky', 'speed_px', 'length_px', 'expected_px', 'gaps', 'at_nozzle', 'survival')} for x in rep['ribbon_resolve']],
       'seta': rep['ribbon_seta'], 'cut': rep['ribbon_cut']}
print(json.dumps(out))
