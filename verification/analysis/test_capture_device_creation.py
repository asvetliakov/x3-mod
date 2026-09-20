"""Host-test the production CreateDevice flag and forwarding paths.

The two CreateDevice bodies are extracted unchanged from capture.cpp and
d3d9_ownership.cpp and compiled against scripted COM-shaped doubles.  This is
control-flow coverage; it does not exercise the x86 ABI or a D3D runtime.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


class CaptureDeviceCreationTests(unittest.TestCase):
    def test_production_capture_and_ownership_create_device(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A host C++ compiler is required")
        capture = (ROOT / "src/proxy/capture.cpp").read_text()
        ownership = (ROOT / "src/ownership/d3d9_ownership.cpp").read_text()
        capture_signature = (
            "HRESULT WINAPI create_device(IDirect3D9* d,UINT adapter,D3DDEVTYPE type,"
            "HWND window,DWORD flags,D3DPRESENT_PARAMETERS* p,IDirect3DDevice9** out)"
        )
        ownership_signature = (
            "HRESULT create_device(Factory* node, UINT adapter, D3DDEVTYPE type, HWND window,\n"
            "                      DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)"
        )
        with tempfile.TemporaryDirectory(prefix="x3-capture-device-creation-") as temporary:
            directory = Path(temporary)
            (directory / "capture_create_device_under_test_inc.h").write_text(
                extract_function(capture, capture_signature)
            )
            (directory / "ownership_create_device_under_test_inc.h").write_text(
                # The same text is a forward declaration near the top of the
                # file; select the later definition before brace extraction.
                extract_function(ownership[ownership.rindex(ownership_signature):],
                                 ownership_signature)
            )
            executable = directory / "capture_device_creation"
            build = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pthread",
                    "-I",
                    str(directory),
                    str(ROOT / "verification/probe/capture_device_creation_fixture.cpp"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(
                run.stdout,
                "capture_device_creation scenarios=19 checks=267 failures=0\n",
            )
            self.assertEqual(run.stderr, "")


if __name__ == "__main__":
    unittest.main()
