#!/usr/bin/env python3
"""Run the synthetic probe with a bounded lifetime in the actual Preview bottle.

This does not start X3, modify bottle configuration, or install a DLL. The
executable is freshly built and its inputs checked for stability; stdout, stderr and invocation metadata are saved.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case",choices=["D24X8_INTZ","D24S8_INTZ","D24X8_DF24","D24X8_D24X8","D24X8_SHADOW","D24S8_FOURCC","D24X8_SHADOW_NODUMMY"],default="D24X8_INTZ")
    args=parser.parse_args()
    stem="depth-resolve-"+args.case.lower().replace("_","-")
    root = Path(__file__).resolve().parents[2]
    executable = root / "verification/probe/build/depth_resolve.exe"
    source = root / "verification/probe/depth_resolve.cpp"
    results = bottle.results_dir(root)
    results.mkdir(parents=True, exist_ok=True)
    paths = [root / name for name in ['verification/probe/depth_resolve.cpp', 'verification/probe/build_depth_resolve.sh', 'verification/probe/run_depth_resolve.py']]
    def source_hashes():
        return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    results.mkdir(parents=True, exist_ok=True)
    before = source_hashes()
    # Always compile the recorded inputs; a stale executable is never evidence.
    try:
        build = subprocess.run(['sh', str(root / 'verification/probe/build_depth_resolve.sh')],
                               cwd=root, capture_output=True, text=True, timeout=60)
        build_code, build_output = build.returncode, build.stdout + build.stderr
    except subprocess.TimeoutExpired:
        build_code, build_output = None, 'Build timed out after 60 seconds'
    after_build = source_hashes()
    if build_code != 0 or after_build != before or not executable.is_file():
        failure = dict(passed=False, freshly_built=False, build_exit_code=build_code,
                       reason='Build failed, executable absent, or source changed during compilation',
                       build_output=build_output, sources_sha256_before=before,
                       sources_sha256_after=after_build)
        (results / (stem+'-summary.json')).write_text(json.dumps(failure, indent=2)+'\n')
        print(json.dumps(failure, indent=2))
        return 1
    command = [
        "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine",
        "--bottle", bottle.BOTTLE, "--no-update", "--workdir", str(executable.parent),
        str(executable), r"C:\X3\d3dx9_37.dll", args.case,
    ]
    metadata = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "bottle": bottle.describe(),
        "command": command,
        "timeout_seconds": 60,
        "process_local_override": "d3d9=b",
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
    }
    metadata.update(freshly_built=True, build_exit_code=build_code, build_output=build_output,
                    sources_sha256=before, sources_unchanged_after_build=after_build == before)
    with (results / (stem+".txt")).open("w") as out, \
            (results / (stem+"-wine.log")).open("w") as err:
        try:
            environment = os.environ.copy()
            environment["WINEDLLOVERRIDES"] = "d3d9=b"
            process = subprocess.run(command, stdout=out, stderr=err, timeout=60, env=environment)
            metadata["exit_code"] = process.returncode
        except subprocess.TimeoutExpired:
            # subprocess.run kills/reaps its launched process on timeout.
            metadata["exit_code"] = None
            metadata["timed_out"] = True
    output = (results / (stem+".txt")).read_text()
    samples = [line for line in output.splitlines() if line.startswith("SAMPLE ")]
    metadata["sample_checks"] = len(samples)
    metadata["passed_sample_checks"] = sum(line.endswith(" PASS") for line in samples)
    states = [line for line in output.splitlines() if line.startswith("STATE ")]
    metadata["state_checks"] = len(states)
    metadata["passed_state_checks"] = sum(line.endswith(" PASS") for line in states)
    expected_samples=108 if args.case.startswith("D24X8_SHADOW") else 36
    expected_states=18 if args.case.startswith("D24X8_SHADOW") else 6
    metadata['sources_unchanged_after_run'] = source_hashes() == before
    metadata['executable_unchanged_after_run'] = (
        hashlib.sha256(executable.read_bytes()).hexdigest() == metadata['executable_sha256'])
    metadata["passed"] = (
        metadata["exit_code"] == 0
        and metadata["sources_unchanged_after_run"]
        and metadata["executable_unchanged_after_run"]
        and len(samples) == expected_samples
        and metadata["passed_sample_checks"] == expected_samples
        and len(states) == expected_states
        and metadata["passed_state_checks"] == expected_states
        and "Reset after resource release: 0x00000000 OK" in output
        and "RESULT PASS:" in output
    )
    (results / (stem+"-summary.json")).write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))
    return 0 if metadata["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
