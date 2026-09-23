"""Host-test production chase-lead geometry and policy with synthetic memory.

This compiles the actual chase_lead_core.h and brace-extracts the bounded read,
scope, ownership/hide, projection, gate, publish, and final-FOV functions from
chase_lead.cpp. It does not qualify the x86 callback ABI, patch bytes, Windows
memory protection, Wine, or live game object layouts.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from verification.analysis.test_capture_bloom_lifetime import extract_function

ROOT = Path(__file__).resolve().parents[2]


def extract_named_function(source: str, name: str) -> str:
    """Find a uniquely named definition while tolerating clang-format spacing."""
    matches = list(re.finditer(
        rf'(?m)^(?:template\s*<[^\n]+>\s*)?[A-Za-z_][^\n;{{}}]*\b{re.escape(name)}\s*\(', source))
    if len(matches) != 1:
        raise ValueError(f'expected one definition for {name}, found {len(matches)}')
    return extract_function(source, matches[0].group(0))


class ChaseLeadHostTests(unittest.TestCase):
    def test_geometry_scope_ownership_and_publication(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        source = (ROOT / 'src/proxy/chase_lead.cpp').read_text()
        names = [
            'read', 'field', 'writable_at', 'active', 'scope', 'projection', 'owned_marker', 'hide', 'note',
            'gate', 'publish', 'hud_texture_available', 'hud_scope', 'owned_hud_nodes',
            'reclaim_hud_anchor_slots', 'finalize_hud_anchor',
            'final_fov', 'central_hud',
            'native_context', 'native_hook', 'handle', 'invalidate_pose', 'camera_context',
        ]
        with tempfile.TemporaryDirectory(prefix='x3-chase-lead-host-') as temporary:
            directory = Path(temporary)
            functions = [extract_named_function(source, name).replace('std::memcpy', 'host_memcpy')
                         for name in names]
            (directory / 'chase_lead_under_test_inc.h').write_text('\n\n'.join(functions))
            executable = directory / 'chase_lead_host'
            build = subprocess.run([
                compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'src/proxy'), '-I', str(directory),
                str(ROOT / 'verification/probe/chase_lead_host.cpp'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'chase_lead_host scenarios=70 checks=273 failures=0\n')
            self.assertEqual(run.stderr, '')


if __name__ == '__main__':
    unittest.main()
