#!/usr/bin/env python3
"""Run the synthetic probe with a bounded lifetime in the actual Preview bottle.

This does not start X3, modify bottle configuration, or install a DLL. The
executable is built separately; stdout, stderr and invocation metadata are saved.
"""
import datetime
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    executable = root / "verification/probe/build/depth_sampling.exe"
    source = root / "verification/probe/depth_sampling.cpp"
    results = root / "verification/results"
    results.mkdir(parents=True, exist_ok=True)
    command = [
        "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine",
        "--bottle", "Steam", "--no-update", "--workdir", str(executable.parent),
        str(executable), r"C:\X3\d3dx9_37.dll",
    ]
    metadata = {
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "command": command,
        "timeout_seconds": 60,
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
    }
    with (results / "depth-sampling.txt").open("w") as out, \
            (results / "depth-sampling-wine.log").open("w") as err:
        try:
            process = subprocess.run(command, stdout=out, stderr=err, timeout=60)
            metadata["exit_code"] = process.returncode
        except subprocess.TimeoutExpired:
            # subprocess.run kills/reaps its launched process on timeout.
            metadata["exit_code"] = None
            metadata["timed_out"] = True
    output = (results / "depth-sampling.txt").read_text()
    samples = [line for line in output.splitlines() if line.startswith("SAMPLE ")]
    metadata["sample_checks"] = len(samples)
    metadata["passed_sample_checks"] = sum(line.endswith(" PASS") for line in samples)
    metadata["passed"] = (
        metadata["exit_code"] == 0
        and len(samples) == 16
        and metadata["passed_sample_checks"] == 16
        and "RESULT PASS:" in output
    )
    (results / "depth-sampling-summary.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))
    return 0 if metadata["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
