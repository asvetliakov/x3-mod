#!/usr/bin/env python3
"""Parent-owned x86 build; explicit isolated output, no Wine execution."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["verification/probe/lattice_upload_readable_fixture.cpp",
           "src/ownership/d3d9_ownership.cpp", "src/ownership/application_admission.cpp",
           "src/ownership/application_admission_abi.cpp", "src/ownership/execution_state.cpp",
           "src/ownership/finite_buffer_evidence.cpp", "src/ownership/portable_managed_upload.cpp"]
FLAGS = ["-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread", "-msse2", "-mfpmath=sse",
         "-mstackrealign", "-mincoming-stack-boundary=2", "-DX3M_FINITE_FIXTURE"]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="i686-w64-mingw32-g++")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    before = time.monotonic()
    commands = []
    objects = []
    dependencies = set()
    for i, name in enumerate(SOURCES):
        obj, dep = out / f"source-{i}.o", out / f"source-{i}.d"
        command = [args.compiler, *FLAGS]
        if name.endswith("application_admission_abi.cpp"):
            command.append("-fno-exceptions")
        command += ["-MMD", "-MF", str(dep), "-c", str(ROOT / name), "-o", str(obj)]
        subprocess.run(command, cwd=ROOT, check=True)
        commands.append(command)
        objects.append(str(obj))
        tokens = dep.read_text().replace("\\\n", " ").split(":", 1)[1].split()
        dependencies.update(Path(token).resolve() for token in tokens)
    exe = out / "lattice_upload_readable_fixture.exe"
    command = [args.compiler, "-static", "-pthread", *objects, "-o", str(exe), "-ldxguid", "-luser32", "-ladvapi32"]
    subprocess.run(command, cwd=ROOT, check=True)
    commands.append(command)
    dependencies.add(Path(__file__).resolve())
    report = {"phase": "readable_metadata", "commands": commands,
              "compiler": subprocess.check_output([args.compiler, "--version"], text=True).splitlines()[0],
              "sources": {str(p.relative_to(ROOT)): sha(p) for p in sorted(dependencies) if p.is_relative_to(ROOT)},
              "exe": str(exe), "exe_sha256": sha(exe), "seconds": time.monotonic()-before,
              "executed": False, "clone_adapter_tested": False, "seh_tested": False}
    (out / "build.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps({k: report[k] for k in ("exe", "exe_sha256", "seconds", "executed")}))


if __name__ == "__main__":
    main()
