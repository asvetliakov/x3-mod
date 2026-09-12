"""Host test of the proxy's export table (W1 of the native-Windows audit).

On Windows the app-local d3d9.dll is what every in-process module gets from
GetModuleHandle("d3d9"), so it must export every name of the system DLL: the
fifteen CrossOver's lib/wine/i386-windows/d3d9.dll exports plus the two
Windows adds (Direct3D9EnableMaximizedWindowedModeShim, Direct3DCreate9On12Ex).
The export directory of build/d3d9.dll is parsed from the file
(tools/analysis/pe_exports.py; skipped when the DLL is not built), and
src/proxy/d3d9.def must list exactly the same names. The Wine fixture
(verification/probe/run_d3d9_exports.py) resolves and calls them.
"""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import pe_exports  # noqa: E402

DLL = ROOT / 'build' / 'd3d9.dll'
DEF = ROOT / 'src' / 'proxy' / 'd3d9.def'


def def_names():
    names = []
    for line in DEF.read_text().splitlines():
        token = line.strip()
        if not token or token.startswith(('LIBRARY', 'EXPORTS', ';')):
            continue
        names.append(token.split()[0])
    return sorted(names)


class ExportTableTests(unittest.TestCase):
    def test_def_lists_the_seventeen_system_names(self):
        self.assertEqual(def_names(), sorted(pe_exports.SYSTEM_D3D9_EXPORTS))
        self.assertEqual(len(pe_exports.SYSTEM_D3D9_EXPORTS), 17)

    def test_loader_defines_every_name(self):
        source = (ROOT / 'src/proxy/loader.cpp').read_text()
        for name in pe_exports.SYSTEM_D3D9_EXPORTS:
            self.assertTrue(f' {name}(' in source or f'X3M_FORWARDED_EXPORT({name},' in source or f'FORWARD_MARKER({name})' in source, name)
        # The signature-agnostic forwarders and their documented fallback argument bytes.
        for name, ret in (('DebugSetLevel', '"ret $4"'), ('PSGPError', '"ret $12"'), ('PSGPSampleTexture', '"ret $20"'),
                          ('Direct3D9EnableMaximizedWindowedModeShim', '"ret $4"')):
            self.assertIn(f'X3M_FORWARDED_EXPORT({name}, {ret})', source)

    @unittest.skipUnless(DLL.is_file(), 'build/d3d9.dll not built')
    def test_built_dll_exports_the_seventeen_names(self):
        table = pe_exports.parse(DLL)
        self.assertEqual(table['dll_name'].lower(), 'd3d9.dll')
        self.assertEqual(table['names'], sorted(pe_exports.SYSTEM_D3D9_EXPORTS))
        self.assertTrue(all(e['forwarder'] is None for e in table['exports']), 'no PE forwarders: every export is our code')
        self.assertEqual(table['machine'], '0x014c')


if __name__ == '__main__':
    unittest.main()
