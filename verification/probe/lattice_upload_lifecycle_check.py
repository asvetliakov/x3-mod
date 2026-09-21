#!/usr/bin/env python3
"""Validate B1 actual-wrapper evidence; explicitly not capture/B2 acceptance."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
REQUIRED = {
    "unarmed actual wrapper count one": 2,
    "exhausted owner refuses before pin acquisition": 2,
    "arm adds exactly one actual wrapper reference": 2,
    "pin query and callback close preserve full CPU and LastError": 2,
    "idle close exact Released": 2,
    "idle close restores caller count one": 2,
    "rearm token never reuses serial": 2,
    "stale close cannot retire newer arm": 2,
    "second actual device cannot steal active Store": 2,
    "old manual disarm API remains compatible": 2,
    "one pin Release outside registry after control detached": 5,
    "one guarded pair copies after getter Releases": 2,
    "paired copy preserves full CPU and LastError": 2,
    "paired metadata matches both observed identities": 2,
    "paired exact bytes and four canaries": 2,
    "stale pair facts refuse both payloads": 8,
    "short IB capacity never copies VB prefix": 2,
    "overlapping payload spans rejected before writes": 2,
    "Store alias rejected without modifying evidence": 2,
    "pending map refuses atomic pair": 2,
    "paired observer adds no native Lock or Unlock": 2,
    "released weak keys refuse without resurrection": 2,
    "active explicit close returns Deferred without waiting": 3,
    "Deferred keeps exact actual wrapper pin count": 3,
    "old manual disarm still refuses active scope": 3,
    "Closing refuses CPU pair before payload access": 3,
    "explicit deferred close preserves real Clone success": 1,
    "normal finish unbinds closes and wipes without later staging": 1,
    "synthetic original C++ boundary propagates with real observer cleanup": 1,
    "synthetic native boundary propagated to outer recovery": 1,
    "boundary abort restores chain and unbinds actual observer": 2,
    "boundary abort drops exactly one actual pin and wipes": 2,
    "scope-only boundary exception counts": 1,
    "last getter Release mutation vetoes both copied payloads": 1,
    "duplicate poisoning remains Busy rather than replacement": 1,
    "actual wrapper forwards injected native Reset failure": 1,
    "failed Reset preserves pin and advances generation": 1,
    "lost generation refuses CPU copy without native access": 1,
    "successful Reset cannot resurrect pre-Reset payload": 1,
    "actual Reset waits behind complete pair registry guard": 1,
    "metadata VB and IB copy complete before native Reset starts": 1,
    "pair barrier native Reset uses creation thread": 1,
    "post-barrier Reset never admits old pair again": 1,
    "readable device no retained references": 12,
    "readable factory no retained references": 12,
}
NEEDED = {
    "src/ownership/clone_upload_observer.h", "src/ownership/clone_upload_observer_inc.h",
    "src/ownership/clone_upload_core.h", "src/ownership/clone_upload_abi.cpp",
    "src/ownership/clone_upload_abi.h", "src/ownership/d3d9_ownership.cpp",
    "verification/probe/lattice_upload_lifecycle_fixture.cpp",
    "verification/probe/lattice_upload_readable_fixture.cpp", "verification/probe/finite_upload_fixture.cpp",
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(text):
    rows = re.findall(r"^CHECK (.+) (PASS|FAIL)$", text, re.MULTILINE)
    assert rows and all(status == "PASS" for _, status in rows), "failed or absent checks"
    counts = Counter(name for name, _ in rows)
    for name, count in REQUIRED.items():
        assert counts[name] == count, f"control count: {name}: {counts[name]} != {count}"
    endings = re.findall(r"^RESULT PASS phase=upload_lifecycle_b1 checks=(\d+) capture_release_tested=0 inherited_callback_seh_tested=0$", text, re.MULTILINE)
    assert len(endings) == 1 and int(endings[0]) == len(rows), "result/count or scope mismatch"
    assert "RESULT FAIL" not in text and "CHECK" not in re.sub(r"^CHECK .+ (PASS|FAIL)$", "", text, flags=re.MULTILINE)
    dll = re.findall(r"^B1_D3DX path=(.+) create_mesh_rva=([0-9a-f]{8}) builtin=0$", text, re.MULTILINE)
    assert len(dll) == 1, "native D3DX provenance required"
    reset = re.findall(r"^PAIR_RESET copy=00000000 reset=([0-9a-f]{8})$", text, re.MULTILINE)
    assert len(reset) == 1, "pair copy/Reset result required"
    return dict(status="PASS", phase="upload_lifecycle_b1", checks=len(rows),
                sessions=12, pair_cases=2, pin_drains=5, deferred_closes=3,
                scope_cpp_unwinds=1, scope_native_unwinds=1,
                d3dx_path=dll[0][0], d3dx_export_rva=dll[0][1], pair_reset_hresult=reset[0],
                capture_release_tested=False, inherited_callback_seh_tested=False,
                native_windows_runtime_tested=False)


def validate_build(build):
    assert build["phase"] == "upload_lifecycle", "wrong build phase"
    paths = []
    for name, expected in build["sources"].items():
        path = Path(name)
        if not path.is_absolute():
            path = ROOT / path
        assert sha(path) == expected, name
        paths.append(path.as_posix())
    for name in NEEDED:
        assert any(path.endswith("/"+name) for path in paths), f"missing source: {name}"
    commands = build["commands"]
    abi = [cmd for cmd in commands if "-c" in cmd and any(x.endswith("/clone_upload_abi.cpp") for x in cmd)]
    assert len(abi) == 2, "ABI requires two objects"
    assert sum("-fno-exceptions" in cmd for cmd in abi) == 1
    assert sum("-DX3M_CLONE_UPLOAD_ABI_EH" in cmd for cmd in abi) == 1
    assert not any("-fno-exceptions" in cmd and "-DX3M_CLONE_UPLOAD_ABI_EH" in cmd for cmd in abi)
    for cmd in commands:
        if "-c" not in cmd:
            continue
        assert not any("X3M_LATTICE_UPLOAD_ABI_FIXTURE" in arg for arg in cmd), "synthetic ABI replacement forbidden"
        assert {"-msse2", "-mfpmath=sse", "-mstackrealign", "-mincoming-stack-boundary=2"} <= set(cmd)
    assert sha(Path(build["exe"])) == build["exe_sha256"], "executable changed"


class CheckerTests(unittest.TestCase):
    def sample(self):
        rows = [f"CHECK {name} PASS" for name, count in REQUIRED.items() for _ in range(count)]
        return "\n".join(rows+["B1_D3DX path=C:\\X3\\d3dx9_37.dll create_mesh_rva=001a5be8 builtin=0",
                              "PAIR_RESET copy=00000000 reset=00000000",
                              f"RESULT PASS phase=upload_lifecycle_b1 checks={len(rows)} capture_release_tested=0 inherited_callback_seh_tested=0"])

    def test_scope_and_failure_controls(self):
        good = self.sample()
        self.assertEqual(validate(good)["pin_drains"], 5)
        bad = [good.replace("PASS", "FAIL", 1), good.replace("checks=", "checks=1"),
               good.replace("capture_release_tested=0", "capture_release_tested=1"),
               good.replace("inherited_callback_seh_tested=0", "inherited_callback_seh_tested=1"),
               good.replace("builtin=0", "builtin=1"), good.replace("PAIR_RESET", "MISSING_RESET"),
               good.replace("copy=00000000", "copy=00000001"),
               "\n".join(good.splitlines()[1:]), good+"\nRESULT FAIL cleanup",
               good+"\nCHECK malformed", good+"\nCHECK paired exact bytes and four canaries PASS"]
        for text in bad:
            with self.subTest(text=text[-80:]):
                with self.assertRaises(AssertionError):
                    validate(text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(CheckerTests))
        raise SystemExit(0 if result.wasSuccessful() else 1)
    if not args.build or not args.log:
        parser.error("--build and --log required")
    build = json.loads(args.build.read_text())
    validate_build(build)
    result = validate(args.log.read_text(errors="strict"))
    result.update(exe_sha256=build["exe_sha256"], log_sha256=sha(args.log))
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
