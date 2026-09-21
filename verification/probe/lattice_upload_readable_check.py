#!/usr/bin/env python3
"""Validate only the focused readable-metadata fixture, never infer Clone/SEH."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
REQUIRED = {
    "metadata exists without finite mode",
    "zero atlas allocation and typed payload work",
    "IB requested Usage independent from actual readable backing",
    "exact VB payload preserved", "exact IB payload preserved",
    "finite position interface remains disabled",
    "Reset advances metadata generation",
    "mapping forwarded without any payload read",
    "no extra native Lock or Unlock",
    "native Lock and Unlock LastError preserved",
    "readable failure retries exact original creation Usage",
    "fallback full incoming and selected outgoing CPU state preserved",
    "fallback remains actual WRITEONLY",
    "metadata admission failure rolls readable conversion back",
    "readable mode rejects missing tracking or typed scanner",
    "readable device no retained references", "readable factory no retained references",
}


def validate(text):
    rows = re.findall(r"^CHECK (.+) (PASS|FAIL)$", text, re.MULTILINE)
    assert rows and all(status == "PASS" for _, status in rows), "failed or absent checks"
    assert REQUIRED <= {name for name, _ in rows}, "missing acceptance control"
    endings = re.findall(r"^RESULT PASS phase=readable_metadata checks=(\d+) clone_adapter_tested=0 seh_tested=0$",
                         text, re.MULTILINE)
    assert len(endings) == 1 and int(endings[0]) == len(rows), "incomplete result/count"
    assert "RESULT FAIL" not in text and "CHECK" not in re.sub(r"^CHECK .+ (PASS|FAIL)$", "", text, flags=re.MULTILINE)
    assert re.findall(r"^RESET hr=([0-9a-f]{8})$", text, re.MULTILINE) == ["00000000"], "Reset HRESULT"
    return {"status": "PASS", "checks": len(rows), "phase": "readable_metadata",
            "clone_adapter_tested": False, "seh_tested": False}


class CheckerTests(unittest.TestCase):
    def sample(self):
        return "\n".join([f"CHECK {name} PASS" for name in sorted(REQUIRED)] +
                         ["RESET hr=00000000",
                          f"RESULT PASS phase=readable_metadata checks={len(REQUIRED)} clone_adapter_tested=0 seh_tested=0"])

    def test_accept_and_refuse_corruption(self):
        self.assertEqual(validate(self.sample())["checks"], len(REQUIRED))
        for bad in (self.sample().replace("PASS", "FAIL", 1),
                    self.sample().replace(f"checks={len(REQUIRED)}", f"checks={len(REQUIRED)-1}"),
                    self.sample().replace("seh_tested=0", "seh_tested=1"),
                    self.sample().replace("RESET hr=00000000", "RESET hr=88760868"),
                    "\n".join(self.sample().splitlines()[1:]),
                    self.sample()+"\nRESULT FAIL late cleanup"):
            with self.subTest(bad=bad[-100:]):
                with self.assertRaises(AssertionError):
                    validate(bad)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(CheckerTests)
        raise SystemExit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
    if not args.build or not args.log:
        parser.error("--build and --log required")
    build = json.loads(args.build.read_text())
    assert build["phase"] == "readable_metadata"
    assert build["clone_adapter_tested"] is False and build["seh_tested"] is False
    for name, expected in build["sources"].items():
        assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest() == expected, name
    assert {"src/ownership/d3d9_ownership.cpp", "src/ownership/d3d9_ownership.h",
            "verification/probe/finite_upload_fixture.cpp",
            "verification/probe/lattice_upload_readable_fixture.cpp"} <= build["sources"].keys()
    assert hashlib.sha256(Path(build["exe"]).read_bytes()).hexdigest() == build["exe_sha256"]
    report = validate(args.log.read_text(errors="strict"))
    report.update(exe_sha256=build["exe_sha256"], log_sha256=hashlib.sha256(args.log.read_bytes()).hexdigest())
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
