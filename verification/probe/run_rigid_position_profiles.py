#!/usr/bin/env python3
"""Verify production profile lookup against local bytes, without a game launch."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ["src/renderer/rigid_position.h", "src/renderer/rigid_position.cpp",
          "src/renderer/rigid_position_profiles_inc.h", "verification/probe/rigid_position_profiles.cpp",
          "verification/probe/run_rigid_position_profiles.py", "verification/results/rigid-position-profiles.json"]
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def sources(): return {p: sha(ROOT/p) for p in INPUTS}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-directory", type=Path, default=Path("/tmp/x3-shader-sweep/programs"))
    args = parser.parse_args()
    build = ROOT/"verification/probe/build"
    results = ROOT/"verification/results"
    build.mkdir(exist_ok=True)
    output = results/"rigid-position-lookup-summary.json"
    before = sources()
    output.write_text(json.dumps(dict(passed=False, phase="building", source_hashes=before), indent=2)+"\n")
    exe = build/"rigid_position_profiles.exe"
    command = ["i686-w64-mingw32-g++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
               "-msse2", "-mfpmath=sse", "-mstackrealign", "-mincoming-stack-boundary=2", "-static",
               str(ROOT/INPUTS[1]), str(ROOT/INPUTS[3]), "-o", str(exe)]
    subprocess.run(command, check=True, timeout=90)
    assert sources() == before
    binary = sha(exe)
    profiles = json.loads((ROOT/INPUTS[-1]).read_text())["programs"]
    observations = []
    paths = []
    launch = ["/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine",
              "--bottle", "Steam", "--no-update", "--workdir", str(build), str(exe)]
    for p in profiles:
        path = args.raw_directory/("vs_"+p["fnv1a64"]+".bin")
        assert sha(path) == p["sha256"], "local shader bytes changed"
        paths.append(path)
        register = p["matrix_first_register"] if p["qualified_position_math"] else -1
        named = any("WorldViewProjection" in name for name in p.get("matrix_names_only", []))
        launch += ["Z:"+str(path.resolve()).replace("/", "\\"), str(register), str(int(named))]
    run = subprocess.run(launch, capture_output=True, timeout=45)
    lines = run.stdout.decode().splitlines()
    assert run.returncode == 0 and len(lines) == len(profiles), run.stdout.decode()
    for p, path, text in zip(profiles, paths, lines):
        assert sha(path) == p["sha256"], "shader changed during run"
        register = p["matrix_first_register"] if p["qualified_position_math"] else -1
        assert text == f"PASS qualified={int(p['qualified_position_math'])} words={p['word_count']} matrix={register} checks=8", text
        observations.append(dict(hash=p["fnv1a64"], sha256=p["sha256"], output=text))
    stable = sources() == before and sha(exe) == binary
    assert stable, "sources or binary changed during run"
    data = dict(passed=stable, programs=len(observations), checks=8*len(observations),
                qualified=sum(p["qualified_position_math"] for p in profiles), game_launched=False,
                source_hashes=before, executable_sha256=binary, build_command=command, command=launch,
                exit_code=run.returncode, source_and_binary_unchanged=stable, cases=observations)
    output.write_text(json.dumps(data, indent=2)+"\n")
    print(json.dumps({k: data[k] for k in ("passed", "programs", "checks", "qualified")}))

if __name__ == "__main__": main()
