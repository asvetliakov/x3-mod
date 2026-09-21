#!/usr/bin/env python3
"""Build/run only the portable CPU core on the host; never Wine or shared DLLs."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix="x3-lattice-upload-host-") as directory:
        executable = Path(directory) / "core-test"
        command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-Wall", "-Wextra",
                   "-Werror", "-pthread", "verification/probe/lattice_upload_core_test.cpp",
                   "-o", str(executable)]
        subprocess.run(command, cwd=ROOT, check=True)
        result = subprocess.run([str(executable)], check=True, text=True, capture_output=True)
        report = json.loads(result.stdout)
        assert report["status"] == "PASS" and report["cases"] == 39
        assert report["arena_bytes"] == 2097152 and report["store_bytes"] <= 2101248
        assert report["native_com_tested"] is False and report["seh_tested"] is False
        print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
