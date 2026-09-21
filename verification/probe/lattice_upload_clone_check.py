#!/usr/bin/env python3
"""Check saved actual CloneMesh evidence; no fixture build or execution."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
# Exact multiplicities distinguish both selected sizes and both declaration paths.
REQUIRED = {
    "arm manual upload": 2, "disarm manual upload": 2,
    "arm source failure": 2, "disarm failed source": 2,
    "arm ignored Unlock failure": 1, "disarm ignored Unlock": 1,
    "arm qualifier callback refusal": 4, "disarm callback refusal": 4,
    "arm unreadable fallback": 1, "disarm unreadable fallback": 1,
    "arm final Release refusal": 1, "disarm final Release refusal": 1,
    "arm concurrent Reset": 1, "disarm concurrent Reset": 1,
    "arm INDEX32 refusal": 1, "disarm INDEX32 refusal": 1,
    "explicit fixture D3DX path supplied": 1,
    "selected D3DX module path available": 1,
    "fixture selected native D3DX for qualified route": 1,
    "actual Clone publishes qualified producer payload": 4,
    "observer adds no destination Lock or Unlock": 4,
    "observer adds no source Lock or Unlock": 4,
    "copy retained with actual final identities": 8,
    "exact retained bytes and canaries": 8,
    "copy success and refusal preserve full CPU state and LastError": 8,
    "exact actual clone bytes": 8,
    "later write invalidates retained record before native mutation": 4,
    "actual Clone reports injected source Lock failure": 2,
    "observed source failure blocks its destination staging": 2,
    "failed source construction wipes speculative arena": 2,
    "actual D3DX ignores injected original destination Unlock failure": 1,
    "independent original Unlock gate vetoes Clone success": 1,
    "failed original closure wipes staged bytes": 1,
    "callback ran after public qualifiers and before final CPU guard": 4,
    "late qualifier callback refuses before raw memcpy": 4,
    "callback refusal cleans context and arena": 4,
    "original Clone succeeds with native WRITEONLY fallback": 1,
    "actual unreadable fallback refuses without mapping read": 1,
    "actual final VB remains WRITEONLY": 1,
    "actual temporary qualifier Release reentered mutation notification": 1,
    "last qualifier Release invalidates both staged parts before publication": 1,
    "final qualifier refusal wipes all private bytes": 1,
    "actual producer parked with registry while Reset entered": 1,
    "native Reset requested on device creation thread": 1,
    "native Reset excluded until guarded raw copy completed": 1,
    "concurrent Reset invalidates old scope before publication": 1,
    "concurrent Reset leaves private arena wiped": 1,
    "unsupported INDEX32 still forwards original public Clone": 1,
    "INDEX32 refused without raw read": 1,
    "readable device no retained references": 13,
    "readable factory no retained references": 13,
}
NEEDED_FILES = {
    "src/ownership/d3d9_ownership.cpp", "src/ownership/d3d9_ownership.h",
    "src/ownership/clone_upload_observer.h", "src/ownership/clone_upload_observer_inc.h",
    "src/ownership/clone_upload_core.h", "src/ownership/clone_upload_abi.h",
    "src/ownership/clone_upload_abi.cpp", "verification/probe/finite_upload_fixture.cpp",
    "verification/probe/lattice_upload_readable_fixture.cpp",
    "verification/probe/lattice_upload_clone_fixture.cpp",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(text):
    rows = re.findall(r"^CHECK (.+) (PASS|FAIL)$", text, re.MULTILINE)
    assert rows and all(status == "PASS" for _, status in rows), "failed or absent checks"
    counts = Counter(name for name, _ in rows)
    for label, count in REQUIRED.items():
        assert counts[label] == count, f"control count: {label}: {counts[label]} != {count}"
    endings = re.findall(r"^RESULT PASS phase=manual_clone checks=(\d+) inherited_callback_seh_tested=0$", text, re.MULTILINE)
    assert len(endings) == 1 and int(endings[0]) == len(rows), "incomplete result/count"
    assert "RESULT FAIL" not in text and "CHECK" not in re.sub(r"^CHECK .+ (PASS|FAIL)$", "", text, flags=re.MULTILINE)
    modules = re.findall(r"^D3DX_MODULE path=(.+)$", text, re.MULTILINE)
    assert len(modules) == 1, "missing explicit D3DX module provenance"
    identity = re.findall(r"^D3DX_IDENTITY wine_builtin_marker=0 create_mesh_rva=([0-9a-f]{8})$", text, re.MULTILINE)
    assert len(identity) == 1, "native D3DX fixture identity required"
    reset = re.findall(r"^CONCURRENT_RESET reset=([0-9a-f]{8}) clone=([0-9a-f]{8})$", text, re.MULTILINE)
    assert len(reset) == 1, "missing actual Reset/Clone HRESULTs"
    return {"status": "PASS", "phase": "manual_clone", "checks": len(rows),
            "exact_byte_cases": 4, "source_failures": 2, "qualifier_refusals": 4,
            "d3dx_module_path": modules[0], "create_mesh_rva": identity[0], "concurrent_reset_hresult": reset[0][0], "concurrent_clone_hresult": reset[0][1],
            "inherited_callback_seh_tested": False, "native_windows_runtime_tested": False}


def validate_build(build):
    assert build["phase"] == "manual_clone"
    paths = []
    for name, expected in build["sources"].items():
        path = Path(name)
        if not path.is_absolute():
            path = ROOT / path
        assert sha(path) == expected, name
        paths.append(path.as_posix())
    for name in NEEDED_FILES:
        assert any(path.endswith("/"+name) for path in paths), f"missing source: {name}"
    commands = build["commands"]
    abi = [cmd for cmd in commands if "-c" in cmd and any(arg.endswith("/clone_upload_abi.cpp") for arg in cmd)]
    assert len(abi) == 2, "two ABI object roles required"
    shell = [cmd for cmd in abi if "-fno-exceptions" in cmd]
    helper = [cmd for cmd in abi if "-DX3M_CLONE_UPLOAD_ABI_EH" in cmd]
    assert len(shell) == len(helper) == 1 and shell[0] != helper[0], "ABI shell/helper roles"
    assert "-DX3M_CLONE_UPLOAD_ABI_EH" not in shell[0]
    for cmd in commands:
        if "-c" not in cmd:
            continue
        assert not any("X3M_LATTICE_UPLOAD_ABI_FIXTURE" in arg for arg in cmd), "synthetic original forbidden"
        assert {"-msse2", "-mfpmath=sse", "-mstackrealign", "-mincoming-stack-boundary=2"} <= set(cmd)
    assert sha(Path(build["exe"])) == build["exe_sha256"], "executable changed"


class CheckerTests(unittest.TestCase):
    def sample(self):
        rows = [f"CHECK {name} PASS" for name, count in REQUIRED.items() for _ in range(count)]
        return "\n".join(rows+["D3DX_MODULE path=C:\\fixture\\d3dx9_37.dll",
                             "D3DX_IDENTITY wine_builtin_marker=0 create_mesh_rva=00100000", "CONCURRENT_RESET reset=00000000 clone=00000000",
                             f"RESULT PASS phase=manual_clone checks={len(rows)} inherited_callback_seh_tested=0"])

    def test_saved_log_discriminators(self):
        good = self.sample()
        self.assertEqual(validate(good)["exact_byte_cases"], 4)
        for bad in (good.replace("PASS", "FAIL", 1), good.replace("checks=", "checks=1"),
                    good.replace("inherited_callback_seh_tested=0", "inherited_callback_seh_tested=1"),
                    "\n".join(good.splitlines()[1:]), good+"\nRESULT FAIL cleanup",
                    good.replace("CONCURRENT_RESET", "MISSING_RESET"),
                    good+"\nCHECK exact actual clone bytes PASS", good+"\nCHECK malformed",
                    good.replace("wine_builtin_marker=0", "wine_builtin_marker=1"),
                    good.replace("D3DX_MODULE path=", "MISSING_MODULE path=")):
            with self.subTest(bad=bad[-100:]):
                with self.assertRaises(AssertionError):
                    validate(bad)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--build", type=Path)
    p.add_argument("--log", type=Path)
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(CheckerTests))
        raise SystemExit(0 if result.wasSuccessful() else 1)
    if not args.build or not args.log:
        p.error("--build and --log required")
    build = json.loads(args.build.read_text())
    validate_build(build)
    report = validate(args.log.read_text(errors="strict"))
    report.update(exe_sha256=build["exe_sha256"], log_sha256=sha(args.log))
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
