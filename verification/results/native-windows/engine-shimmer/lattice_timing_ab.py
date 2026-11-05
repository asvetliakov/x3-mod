"""Shimmer lattice-index fix: GPU cost A/B on the native host (2026-11-05). Usage, from the checkout, under the lock:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 <this script> OLD.exe NEW.exe [rounds]
OLD.exe: the shimmer fixture built with the program before the fix (engine_shimmer_pixel_program_inc.h at df439ff3b),
NEW.exe: built by run_engine_shimmer.py with the fixed program. Runs `--only timing` alternately, prints the median
gpu_ms per size and rect count for each and the new/old ratio. Never the game."""
import re
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'verification/probe'))
import native_host

old, new = Path(sys.argv[1]), Path(sys.argv[2])
rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 3
rows = {'old': {}, 'new': {}}
for _ in range(rounds):
    for name, exe in (('old', old), ('new', new)):
        done, text = native_host.run_native(exe, ['--only', 'timing'], exe.parent, 300)
        for m in re.finditer(r'^TIMING width=(\d+) .*?rects=(\d+) .*?gpu_ms=([\d.]+)', text, re.M):
            rows[name].setdefault(f'{m[1]}_{m[2]}', []).append(float(m[3]))
for key in rows['old']:
    a, b = statistics.median(rows['old'][key]), statistics.median(rows['new'][key])
    print(f'{key:>8} old {a:.4f} new {b:.4f} ratio {b / a:.3f} old_runs {rows["old"][key]} new_runs {rows["new"][key]}')
