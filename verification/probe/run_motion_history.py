#!/usr/bin/env python3
"""Fresh Win32/SSE2 original CPU correspondence fixture; never launches X3."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ["src/renderer/motion_history.h", "src/renderer/motion_history.cpp",
          "verification/probe/motion_history.cpp", "verification/probe/run_motion_history.py"]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def sources():
    return {p: digest(ROOT / p) for p in INPUTS}

def main():
    build = ROOT / "verification/probe/build"
    results = bottle.results_dir(ROOT)
    build.mkdir(exist_ok=True); results.mkdir(exist_ok=True)
    exe = build / "motion_history.exe"
    summary = results / "motion-history-summary.json"
    summary.write_text(json.dumps(dict(passed=False, phase="reading_inputs"), indent=2) + "\n")
    before = sources()
    summary.write_text(json.dumps(dict(passed=False, phase="building", source_hashes=before), indent=2) + "\n")
    command = ["i686-w64-mingw32-g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               "-msse2", "-mfpmath=sse", "-mstackrealign", "-mincoming-stack-boundary=2", "-static",
               str(ROOT / INPUTS[1]), str(ROOT / INPUTS[2]), "-o", str(exe)]
    subprocess.run(command, check=True, timeout=90)
    assert sources() == before
    binary = digest(exe)
    launch = ["/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine",
              "--bottle", bottle.BOTTLE, "--no-update", "--workdir", str(build), str(exe)]
    result = subprocess.run(launch, capture_output=True, timeout=45)
    report = results / "motion-history.txt"; report.write_bytes(result.stdout)
    (results / "motion-history-wine.log").write_bytes(result.stderr)
    match = re.fullmatch(rb"RESULT PASS checks=(\d+)\r?\n", result.stdout)
    data = dict(passed=result.returncode == 0 and bool(match) and int(match[1]) == 3404 and sources() == before and digest(exe) == binary,
                checks=int(match[1]) if match else 0, game_launched=False, bottle=bottle.describe(), build_command=command,
                command=launch, source_hashes=before, executable_sha256=binary,
                source_unchanged=sources() == before, executable_unchanged=digest(exe) == binary,
                report_sha256=digest(report), exit_code=result.returncode)
    summary.write_text(json.dumps(data, indent=2) + "\n")
    print(json.dumps(data, indent=2))
    return 0 if data["passed"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
