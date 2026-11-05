"""Attribution run (2026-11-05). Usage, from the checkout, with a detached worktree of the Wine record's commit:
  git worktree add --detach WT 0cf873a6b   (ribbons: add the Scene::frame BeginScene / EndScene bracket there)
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 <this script> WT > attribution_0cf873a6b.json
Builds the shimmer fixture from the worktree at the Wine record's commit (unchanged) and runs it natively."""
import json, sys
from pathlib import Path
WT = Path(sys.argv[1])
sys.path.insert(0, str(WT / 'verification/probe'))
import run_engine_shimmer as r
sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'verification/probe'))
import native_host
b = r.build()
done, text = native_host.run_native(r.EXE, [], r.BUILD, 900, log=r.BUILD / 'fixture-native')
rep = r.parse(text)
print(json.dumps({'warnings': b['warnings'], 'exit': done.returncode, 'checks': rep['check_count'], 'failed': rep['failed_checks'],
                  'state': rep['state'], 'fatal': [l for l in text.splitlines() if l.startswith(('FATAL', 'RESULT'))]}))
