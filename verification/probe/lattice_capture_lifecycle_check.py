#!/usr/bin/env python3
"""Saved B2a actual Capture lifecycle evidence, never launches Wine or builds."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import unittest

REQUIRED = {
    "actual readable ownership and Capture factory": 8,
    "actual factory final count zero": 8,
    "actual Capture retires device exactly once and disables gate": 6,
    "default-off Release performs no upload query": 4,
    "direct application final Release returns actual zero": 3,
    "app Release leaves legitimate child and upload pin": 1,
    "actual final child Release completes": 1,
    "explicit immediate close exact Released": 1,
    "explicit close releases one actual device pin": 1,
    "stale close cannot disable newer A gate": 1,
    "second device arm refuses without disabling first gate": 1,
    "unarmed second device final Release returns zero": 1,
    "unrelated device retirement preserves active upload owner": 1,
    "routed final-release case has actual renderer-owned references": 1,
    "failed Capture Reset keeps pin and advances both generations": 1,
    "actual Capture native Reset recovery": 1,
    "Reset never silently rearms Store": 1,
    "active actual Clone close returns Deferred without waiting": 1,
    "Deferred leaves active scope pin with stack guard unwound": 1,
    "actual Clone unchanged success with one deferred close": 1,
    "last app Release during real Clone returns exact child-protected count": 1,
    "actual Clone finish drains pin after unbinding": 1,
    "query final Release preserves native submission once outside guard": 2,
    "query native incoming outgoing CPU and LastError preserved": 2,
    "query and draw pins remain balanced without reentrant restoration": 2,
    "draw pin retirement accounts for actual renderer and upload references": 2,
}


def validate(text):
    rows = re.findall(r"^CHECK (.+) (PASS|FAIL)$", text, re.MULTILINE)
    assert rows and all(value == "PASS" for _, value in rows), "failed/missing assertions"
    counts = Counter(name for name, _ in rows)
    for label, expected in REQUIRED.items():
        assert counts[label] == expected, f"{label}: {counts[label]} != {expected}"
    assert "FAIL" not in text, "failure output"
    result = re.findall(r"^RESULT PASS phase=capture_lifecycle_b2a checks=(\d+) actual_capture=1 ownership=1 payload_serialization=0 inherited_callback_seh_tested=0$", text, re.MULTILINE)
    assert len(result) == 1 and int(result[0]) == len(rows), "final count/scope"
    assert re.findall(r"^CAPTURE_LIFETIME mode=(\d+) retired=1$", text, re.MULTILINE) == ["0", "1", "2", "3"]
    deferred = re.findall(r"^CAPTURE_DEFERRED explicit_close=1 child_protected_release=(\d+) actual_clone=1$", text, re.MULTILINE)
    assert len(deferred) == 1 and int(deferred[0]) > 0, "deferred is explicit child-protected close"
    query = re.findall(r"^CAPTURE_QUERY routed=(\d+) renderer_before=(\d+) live=0 retired=1 query_retired=1$", text, re.MULTILINE)
    assert len(query) == 2 and [row[0] for row in query] == ["0", "1"], "guarded lifecycle matrix"
    assert int(query[1][1]) > 0, "no positive actual renderer reference witness"
    dll = re.findall(r"^CAPTURE_D3DX path=(.+) builtin=0$", text, re.MULTILINE)
    assert len(dll) == 1, "native D3DX provenance"
    return dict(status="PASS", phase="capture_lifecycle_b2a", checks=len(rows), devices=9,
                actual_capture=True, ownership=True, guarded_draws=2, explicit_deferred_close=True,
                deferred_release_count=int(deferred[0]), renderer_references=int(query[1][1]),
                payload_serialization=False, inherited_callback_seh_tested=False,
                native_windows_runtime_tested=False, d3dx_path=dll[0])


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def validate_build(build, run):
    assert build["phase"] == "capture_lifecycle_b2a"
    needed = {"src/proxy/capture.cpp", "src/proxy/capture.h", "src/proxy/lattice_upload_hook.cpp",
              "src/ownership/clone_upload_observer_inc.h", "src/ownership/clone_upload_core.h",
              "src/ownership/clone_upload_abi.cpp", "src/ownership/d3d9_ownership.cpp",
              "verification/probe/lattice_capture_lifecycle_abi.h",
              "verification/probe/lattice_capture_lifecycle_fixture.cpp",
              "verification/probe/motion_output_fixture.cpp",
              "verification/probe/lattice_observer_guard_seam_inc.h"}
    for path, expected in build["sources"].items():
        assert Path(path).is_absolute() and sha(path) == expected, f"source changed: {path}"
    for name in needed:
        assert any(path.endswith("/" + name) for path in build["sources"]), f"source missing: {name}"
    for kind in ("exe", "dll"):
        assert sha(build[kind]) == build[kind + "_sha256"] == run[kind + "_sha256"]
    assert run["returncode"] == 0 and run["bottle"] == "X3" and run["WineArch"] == "arm64"
    assert run["FEX_X87REDUCEDPRECISION"] == "1" and run["WINEMSYNC"] == "1" and run["game_launched"] is False
    assert any(value.endswith("/wine_lock.py") for value in run["command"]), "serialized owner run required"
    assert "--bottle" in run["command"] and "X3" in run["command"]
    return dict(source_bindings=len(build["sources"]), exe_sha256=build["exe_sha256"], dll_sha256=build["dll_sha256"],
                seconds=run["seconds"], bottle="X3", WineArch="arm64")


class Tests(unittest.TestCase):
    def sample(self):
        rows = [f"CHECK {key} PASS" for key, n in REQUIRED.items() for _ in range(n)]
        rows += [f"CAPTURE_LIFETIME mode={i} retired=1" for i in range(4)]
        rows += ["CAPTURE_DEFERRED explicit_close=1 child_protected_release=3 actual_clone=1",
                 "CAPTURE_QUERY routed=0 renderer_before=0 live=0 retired=1 query_retired=1",
                 "CAPTURE_QUERY routed=1 renderer_before=7 live=0 retired=1 query_retired=1",
                 "CAPTURE_D3DX path=C:\\X3\\d3dx9_37.dll builtin=0",
                 f"RESULT PASS phase=capture_lifecycle_b2a checks={sum(REQUIRED.values())} actual_capture=1 ownership=1 payload_serialization=0 inherited_callback_seh_tested=0"]
        return "\n".join(rows)

    def test_negative_controls(self):
        text = self.sample()
        self.assertEqual(validate(text)["devices"], 9)
        bad = [text.replace("PASS", "FAIL", 1), text.replace("ownership=1", "ownership=0"),
               text.replace("explicit_close=1", "explicit_close=0"),
               text.replace("renderer_before=7", "renderer_before=0"),
               text.replace("child_protected_release=3", "child_protected_release=0"),
               text.replace("live=0", "live=1", 1), text.replace("builtin=0", "builtin=1"),
               text.replace("payload_serialization=0", "payload_serialization=1"),
               text.replace("checks=", "checks=9"), "\n".join(text.splitlines()[1:])]
        for value in bad:
            with self.assertRaises(AssertionError):
                validate(value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--run", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
        raise SystemExit(not result.wasSuccessful())
    if not args.log or not args.build:
        parser.error("--log and --build required")
    run_path = args.run or args.build.parent / "run.json"
    binding = validate_build(json.loads(args.build.read_text()), json.loads(run_path.read_text()))
    result = validate(args.log.read_text())
    result.update(binding)
    result["log_sha256"] = sha(args.log)
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
