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


# The FL site table (src/engine/fl/weapon_effects_sites_fl.h; docs/reverse-engineering/fl/weapon-effect-spawn.md):
# X3FL.exe is relocatable, so every window is an RVA and two carry a relocated absolute operand masked in the compare.
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FL_EXE_SHA256 = 'e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8'
FL_HEADER = ROOT / 'src/engine/fl/weapon_effects_sites_fl.h'


def fl_exe_at(data, rva, size):
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    n, opt = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    for i in range(n):
        vs, va, rs, ra = struct.unpack_from('<IIII', data, pe + 24 + opt + 40 * i + 8)
        if va <= rva < va + max(vs, rs):
            off = ra + rva - va
            return data[off:off + size]
    raise AssertionError(f'RVA {rva:#x} not in any section')


FL_SLOTS = {'gate_slot_rva': 0x1a82ec, 'type_table_rva': 0x1a83c8}
FL_PREFERRED_BASE = 0x400000


def fl_windows():
    """The eight FL site windows and the nine frame witnesses: list of (name, rva, bytes, fixup_offset, fixup_rva)."""
    text = FL_HEADER.read_text()
    arrays = {m.group(1): bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})', m.group(2)))
              for m in re.finditer(r'constexpr unsigned char (\w+)\[\d+\] = \{([^}]*)\}', text)}
    out = []
    for name, rva, length, var, fixoff, fixrva in re.findall(
            r'\{"(\w+)", (0x[0-9a-f]+), (\d+), (\w+), (no_fixup|\d+), (\w+), 0x[0-9a-f]+\}', text):
        out.append((name, int(rva, 16), arrays[var], None if fixoff == 'no_fixup' else int(fixoff),
                    FL_SLOTS.get(fixrva)))
    return out


def fl_relocations(data):
    """Every HIGHLOW base relocation RVA of the image (.reloc through the data directory)."""
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    rva, size = struct.unpack_from('<II', data, pe + 24 + 96 + 8 * 5)
    out, at, end = set(), rva, rva + size
    while at < end:
        page, block = struct.unpack('<II', fl_exe_at(data, at, 8))
        if block < 8:
            break
        entries = fl_exe_at(data, at + 8, block - 8)
        for k in range(0, len(entries), 2):
            e = struct.unpack_from('<H', entries, k)[0]
            if e >> 12 == 3:
                out.add(page + (e & 0xfff))
        at += block
    return out


class FlWeaponEffectsSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not FL_EXE.is_file():
            raise unittest.SkipTest(f'{FL_EXE} not present')
        cls.data = FL_EXE.read_bytes()

    def test_installed_fl_executable_is_the_pinned_one(self):
        self.assertEqual(hashlib.sha256(self.data).hexdigest(), FL_EXE_SHA256)

    def test_every_fl_window_matches_the_executable_with_the_fixup_masked(self):
        windows = fl_windows()
        self.assertEqual(len(windows), 17)  # eight sites (the batch submit too), six impact witnesses, two muzzle, one death
        self.assertEqual(sum(1 for w in windows if w[3] is not None), 3)  # object, muzzle, source_test
        relocs = fl_relocations(self.data)
        for name, rva, expected, fixoff, fixrva in windows:
            live = bytearray(fl_exe_at(self.data, rva, len(expected)))
            inside = sorted(r - rva for r in relocs if rva <= r < rva + len(expected))
            if fixoff is not None:
                # the masked dword is the preferred-base address of its target, and the loader's only fixup in the window
                self.assertEqual(struct.unpack_from('<I', live, fixoff)[0], FL_PREFERRED_BASE + fixrva, name)
                self.assertEqual(inside, [fixoff], f'FL window {name}: relocations {inside}')
                live[fixoff:fixoff + 4] = b'\0\0\0\0'
            else:
                self.assertEqual(inside, [], f'FL window {name}: relocations {inside}')
            self.assertEqual(bytes(live), expected, f'FL window {name} at rva {rva:#x}')

    def test_fl_header_static_asserts_compile(self):
        compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not compiler:
            self.skipTest('a host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-fl-weapon-sites-') as tmp:
            src = Path(tmp) / 's.cpp'
            src.write_text('#include "weapon_effects_sites_fl.h"\nint main(){ return 0; }\n')
            subprocess.run([compiler, '-std=c++17', '-I', str(ROOT / 'src/engine/fl'), str(src), '-o', str(Path(tmp) / 's')],
                           check=True)


if __name__ == '__main__':
    unittest.main()
