"""Compiler provenance must identify the DLL even when a shader has includes.

Use a fake compiler process to isolate record assembly; this does not claim
native shader compilation or GPU execution coverage.
"""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('shader_compiler_provenance_tool',
    ROOT / 'tools/shaders/generate_rigid_motion_pixel.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class CompilerProvenance(unittest.TestCase):
    def test_included_source_does_not_replace_compiler_identity(self):
        with tempfile.TemporaryDirectory(prefix='.shader-provenance-', dir=ROOT) as directory:
            work = Path(directory)
            source, include = work / 'test.hlsl', work / 'shared.hlsl'
            source.write_text('#include "shared.hlsl"\nfloat4 main():COLOR0 { return value; }\n')
            include.write_text('static const float4 value = 0;\n')
            compiler = work / 'test-compiler.dll'
            compiler.write_bytes(b'fake external compiler identity')
            header, provenance = work / 'program_inc.h', work / 'program.json'
            entry = dict(source=source, header=header, provenance=provenance)

            def fake_run(command, **kwargs):
                if command[0] == 'i686-w64-mingw32-g++':
                    return
                # The external compiler's output argument, before the profile.
                Path(command[-2][2:]).write_bytes(struct.pack('<II', 0xffff0300, 0xffff))

            with patch.dict(generator.SHADERS, {'provenance_test': entry}), \
                 patch.object(generator.subprocess, 'run', side_effect=fake_run):
                generator.compile_one('provenance_test', SimpleNamespace(d3dx=compiler, check=False))
            record = json.loads(provenance.read_text())
            self.assertEqual(record['compiler_sha256'], generator.sha(compiler.read_bytes()))
            self.assertEqual(record['includes'][str(include.relative_to(ROOT))], generator.sha(include.read_bytes()))
            self.assertNotEqual(record['compiler_sha256'], next(iter(record['includes'].values())))


if __name__ == '__main__':
    unittest.main()
