"""Host checks of the weapon-effects hook sites (src/proxy/weapon_effects_sites.h): the window, witness and tap bytes
pinned against the installed X3AP.exe (SHA-256 fdbf3418...f8ab) at their preferred VAs, and the header's own
static_asserts compiled on the host (the E8 rel32 targets, the qword rules, the frame-slot derivation). No Wine, no
game. The EXE is read read-only; its bytes are not copied into the repository.
"""
import hashlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXE = Path('/mnt/c/X3/X3AP.exe')
EXE_SHA256 = 'fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab'
HEADER = ROOT / 'src/proxy/weapon_effects_sites.h'
IMAGE_BASE = 0x00400000


def header_constants():
    """The byte arrays and VAs the header declares, parsed from its text (no C++ needed to read them)."""
    text = HEADER.read_text()

    def bytes_of(name):
        m = re.search(r'expected_%s\[[^\]]*\]\s*=\s*\{([^}]*)\}' % name, text, re.S)
        return bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(1)))

    def va(name):
        return int(re.search(r'%s\s*=\s*(0x[0-9a-fA-F]+)' % name, text).group(1), 16)

    windows = {'impact': (va('impact_window_va'), bytes_of('impact_window')),
               'prologue': (va('prologue_va'), bytes_of('prologue'))}
    taps = re.search(r'expected_tap_window\[2\]\[tap_window_length\]\s*=\s*\{(.*?)\};', text, re.S).group(1)
    groups = re.findall(r'\{([^}]*)\}', taps)
    tap_va = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', re.search(r'tap_window_va\[2\]\s*=\s*\{([^}]*)\}', text).group(1))]
    for i, g in enumerate(groups[:2]):
        windows[f'tap{i + 1}'] = (tap_va[i], bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', g)))
    witnesses = []
    for m in re.finditer(r'\{(0x[0-9a-fA-F]+),\s*(\d+),\s*\{([^}]*)\}\}', text):
        witnesses.append((int(m.group(1), 16), int(m.group(2)),
                          bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', m.group(3)))))
    return windows, witnesses


def read_exe():
    data = EXE.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    n = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<8sIIII', data, pe + 24 + opt + 40 * i)[1:5] for i in range(n)]

    def at(va, size):
        rva = va - IMAGE_BASE
        for vs, sva, rs, ra in sections:
            if sva <= rva < sva + vs:
                off = ra + rva - sva
                return data[off:off + size]
        raise AssertionError(f'VA {va:#x} not in any section')
    return data, at


class WeaponEffectsSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not EXE.is_file():
            raise unittest.SkipTest(f'{EXE} not present')
        cls.data, _at = read_exe()
        cls.at = staticmethod(_at)

    def test_installed_executable_is_the_pinned_one(self):
        self.assertEqual(hashlib.sha256(self.data).hexdigest(), EXE_SHA256)

    def test_window_witness_and_tap_bytes_match_the_executable(self):
        windows, witnesses = header_constants()
        for name, (va, expected) in windows.items():
            self.assertEqual(self.at(va, len(expected)), expected, f'{name} window at {va:#x}')
        for va, length, expected in witnesses:
            self.assertEqual(self.at(va, length), expected[:length], f'witness at {va:#x}')

    def test_impact_call_targets_0x413c40(self):
        windows, _ = header_constants()
        va, win = windows['impact']
        # the E8 is at offset 2 (push edi; push esi; call); its rel32 reaches 0x00413c40
        self.assertEqual(win[2], 0xE8)
        rel = struct.unpack_from('<i', win, 3)[0]
        self.assertEqual((va + 2 + 5 + rel) & 0xffffffff, 0x00413c40)

    def test_header_static_asserts_compile(self):
        compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not compiler:
            self.skipTest('a host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-weapon-sites-') as tmp:
            src = Path(tmp) / 's.cpp'
            src.write_text('#include "weapon_effects_sites.h"\nint main(){ return 0; }\n')
            subprocess.run([compiler, '-std=c++17', '-I', str(ROOT / 'src/proxy'), str(src), '-o', str(Path(tmp) / 's')],
                           check=True)


if __name__ == '__main__':
    unittest.main()
