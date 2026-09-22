"""Real MotionOutput handoff/resolve-attempt control flow, scripted D3D outcomes.

These host checks do not qualify GPU restoration, Windows ABI or asynchronous
invocation ownership. Production headers and the five named function bodies
are compiled unchanged; the heavy renderer and other dependencies are fakes.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MotionHdrSceneTests(unittest.TestCase):
    def test_synchronous_handoff_and_default_null_parity(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        ranges = [
            ('bool MotionOutput::resolve_hdr(', '\nvoid MotionOutput::before_stretch('),
            ('bool MotionOutput::resolve_allowed(', '\n// The engine scene-end signal'),
            # scene_end_hook with publish_sun_lane and note_sun_untracked_writer, up to the projection constants.
            ('void MotionOutput::scene_end_hook(', '\nnamespace {\n// Default projection scratch'),
            ('renderer::HdrWriteback MotionOutput::hdr_writeback(', '\nvoid MotionOutput::flush_redirect('),
            ('void MotionOutput::end_redirect(', '\nvoid MotionOutput::drop_redirect('),
        ]
        functions = []
        for first, last in ranges:
            start = source.index(first)
            functions.append(source[start:source.index(last, start)])
        with tempfile.TemporaryDirectory(prefix='x3-motion-hdr-scene-') as temporary:
            directory = Path(temporary)
            (directory / 'motion_hdr_scene_under_test_inc.h').write_text('\n'.join(functions))
            executable = directory / 'handoff'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT / 'verification/probe/motion_hdr_scene_stubs'), '-I', str(directory),
                                    str(ROOT / 'verification/probe/motion_hdr_scene_fixture.cpp'),
                                    str(ROOT / 'src/renderer/exposure.cpp'),
                                    str(ROOT / 'src/renderer/motion_row_history.cpp'), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^motion_hdr_scene scenarios=72 checks=\d+ failures=0\n$')
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
