"""Host tests of the FL camera provider (src/engine/fl/camera_state_fl.cpp).

docs/architecture/farnhams-legacy-camera.md. (a) The generated anchors (camera_state_fl_inc.h) are
decoded from X3FL.exe on disk: the two publication sites are whole instructions, the store operands
are the documented view/perspective slot RVAs in .data (not the orthographic slot), the disp32 is the
only relocated word, and a Python mirror of the DLL's in-memory check accepts the span relocated to
FL run 1's live base 0x4d0000 and refuses a flipped byte or a foreign base. (b) The sanity arithmetic
of the fl.camera_sample row (camera_sample_sanity.h) is compiled natively and fed a row-vector
perspective camera, the orthographic form and a skewed view. (c) Edition wiring. X3FL.exe is only read.
"""
import importlib.util
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FL_EXE = Path('/mnt/c/X3FL/X3FL.exe')
FRAGMENT = ROOT / 'src/engine/fl/camera_state_fl_inc.h'
PROVIDER = ROOT / 'src/engine/fl/camera_state_fl.cpp'
SANITY = ROOT / 'src/engine/fl/camera_sample_sanity.h'
LIVE_BASE = 0x4d0000  # FL run 1 (docs/verification/farnhams-legacy.md)

spec = importlib.util.spec_from_file_location('fl_camera_constants', ROOT / 'tools/analysis/fl_camera_constants.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

DRIVER = r'''
#include "camera_sample_sanity.h"
#include <cstdio>
using namespace x3m::fl::camera_sanity;
int main() {
    float p[16], v[16], prev[32] = {};
    bool have = false;
    while (true) {
        for (int i = 0; i < 16; ++i) if (std::scanf("%f", &p[i]) != 1) return 0;
        for (int i = 0; i < 16; ++i) if (std::scanf("%f", &v[i]) != 1) return 1;
        Summary s; summarize(p, v, s);
        const bool c = have && changed(p, v, prev); remember(p, v, prev); have = true;
        std::printf("%s %u %.9g %.9g %.9g %.9g %u\n", shape_name(s.shape), s.finite, s.aspect, s.ortho_dot_max,
                    s.ortho_len_err_max, s.translation_length, c);
    }
}
'''


def constants():
    """The anchors of the generated C++ fragment."""
    text = FRAGMENT.read_text()
    out = {}
    for m in re.finditer(r'constexpr CameraAnchor (\w+)_anchor = \{(0x[0-9a-f]+), (0x[0-9a-f]+), (\d+), (\d+), \{([^}]*)\}\};', text):
        out[m.group(1)] = {'site_rva': int(m.group(2), 16), 'slot_rva': int(m.group(3), 16), 'operand_offset': int(m.group(4)),
                           'length': int(m.group(5)), 'bytes': bytes(int(b, 16) for b in m.group(6).split(','))}
    out['orthographic_slot_rva'] = int(re.search(r'orthographic_slot_rva = (0x[0-9a-f]+)', text).group(1), 16)
    return out


def anchor_matches(image_read, base, a):
    """Python mirror of camera_state_fl.cpp anchor_matches(): image_read(address, size) -> bytes or None."""
    actual = image_read(base + a['site_rva'], a['length'])
    if actual is None:
        return False
    operand = struct.unpack_from('<I', actual, a['operand_offset'])[0]
    if operand != (base + a['slot_rva']) & 0xffffffff:
        return False
    masked = bytearray(actual)
    masked[a['operand_offset']:a['operand_offset'] + 4] = b'\0\0\0\0'
    return bytes(masked) == a['bytes']


def relocated_span(data, fields, a, base):
    """The file's span at the site with its one HIGHLOW relocation applied for `base` (what the loader maps)."""
    off = gen.identity.rva_to_offset(fields, a['site_rva'])
    span = bytearray(data[off:off + a['length']])
    value = struct.unpack_from('<I', span, a['operand_offset'])[0] - fields['image_base'] + base
    struct.pack_into('<I', span, a['operand_offset'], value & 0xffffffff)
    return bytes(span)


def need(path):
    if not path.exists():
        raise unittest.SkipTest(f'{path} absent')
    return path.read_bytes()


def perspective(aspect=5120 / 1440, fov_y=math.radians(70), near=1.0, far=50000.0):
    """Row-vector D3D perspective (D3DXMatrixPerspectiveFovLH)."""
    m11 = 1 / math.tan(fov_y / 2)
    m00 = m11 / aspect
    q = far / (far - near)
    return [m00, 0, 0, 0, 0, m11, 0, 0, 0, 0, q, 1, 0, 0, -q * near, 0]


def orthographic(w=5120.0, h=1440.0, near=0.0, far=1.0):
    return [2 / w, 0, 0, 0, 0, 2 / h, 0, 0, 0, 0, 1 / (far - near), 0, 0, 0, -near / (far - near), 1]


def view(yaw, pitch, position):
    """Row-vector view: columns are the camera basis, translation -position . basis."""
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    right = (cy, 0, -sy)
    forward = (sy * cp, sp, cy * cp)
    up = (-sy * sp, cp, -cy * sp)
    v = [right[0], up[0], forward[0], 0, right[1], up[1], forward[1], 0, right[2], up[2], forward[2], 0, 0, 0, 0, 1]
    for k, axis in enumerate((right, up, forward)):
        v[12 + k] = -sum(position[i] * axis[i] for i in range(3))
    return v


class FlCameraAnchors(unittest.TestCase):
    def test_fragment_is_generated_from_x3fl(self):
        data = need(FL_EXE)
        self.assertEqual(FRAGMENT.read_text(), gen.fragment(data, gen.identity.parse(data)))

    def test_sites_decode_to_the_documented_slots(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        found = {a['name']: a for a in gen.anchors(data, fields)}
        c = constants()
        self.assertEqual((found['view']['site_rva'], found['view']['slot_rva']), (0xda01a, 0x1aae68))
        self.assertEqual((found['projection']['site_rva'], found['projection']['slot_rva']), (0xda036, 0x1aae78))
        self.assertEqual(found['view']['allocator_rva'], 0xd8fc0)
        for name in ('view', 'projection'):
            self.assertEqual(c[name]['bytes'], found[name]['bytes'])
            # The file holds the preferred-base VA in the store operand; the store itself is at site + 10.
            self.assertEqual(found[name]['file_bytes'][10], 0xa3)
            self.assertEqual(struct.unpack_from('<I', found[name]['file_bytes'], 11)[0], fields['image_base'] + c[name]['slot_rva'])
            self.assertNotEqual(c[name]['slot_rva'], c['orthographic_slot_rva'])
        # The orthographic allocation stores through EDI at 0x4da05e (`mov [0x5aae4c], edi`), not an anchored site.
        off = gen.identity.rva_to_offset(fields, 0xda05e)
        self.assertEqual(data[off:off + 2], b'\x89\x3d')
        self.assertEqual(struct.unpack_from('<I', data, off + 2)[0], fields['image_base'] + c['orthographic_slot_rva'])
        # The slots are zero-initialised image data (.data beyond its raw bytes): nothing to read in the file.
        data_section = next(s for s in fields['sections'] if s[0].rstrip(b'\0') == b'.data')
        for name in ('view', 'projection'):
            self.assertGreaterEqual(c[name]['slot_rva'], data_section[2] + data_section[3])
            self.assertLess(c[name]['slot_rva'] + 4, data_section[2] + data_section[1])

    def test_in_memory_mirror_accepts_the_relocated_span_and_refuses_mutations(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        c = constants()
        for name in ('view', 'projection'):
            a = c[name]
            for base in (fields['image_base'], LIVE_BASE, 0x10000000):
                image = {base + a['site_rva']: relocated_span(data, fields, a, base)}
                read = lambda address, size, image=image: image.get(address) if len(image.get(address, b'')) == size else None
                self.assertTrue(anchor_matches(read, base, a), (name, hex(base)))
                # The span mapped for one base, checked against another: the operand no longer equals base + slot.
                self.assertFalse(anchor_matches(read, base + 0x10000, a))
                for i in range(a['length']):
                    flipped = bytearray(image[base + a['site_rva']])
                    flipped[i] ^= 0x01
                    read_flipped = lambda address, size, span=bytes(flipped): span if size == len(span) else None
                    self.assertFalse(anchor_matches(read_flipped, base, a), (name, hex(base), i))
                self.assertFalse(anchor_matches(lambda address, size: None, base, a))

    def test_generator_refuses_a_moved_relocation_or_a_foreign_slot(self):
        data = need(FL_EXE)
        fields = gen.identity.parse(data)
        off = gen.identity.rva_to_offset(fields, 0xda024 + 1)
        foreign = bytearray(data)
        struct.pack_into('<I', foreign, off, fields['image_base'] + 0x1aae4c)  # the orthographic slot in the view store
        with self.assertRaises(ValueError):
            gen.anchors(bytes(foreign), fields)
        opcode = bytearray(data)
        opcode[off - 1] = 0x89  # not `mov [disp32], eax`
        with self.assertRaises(ValueError):
            gen.anchors(bytes(opcode), fields)


class FlCameraSanity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++') or shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('no host C++ compiler')
        cls.tmp = tempfile.TemporaryDirectory()
        source = Path(cls.tmp.name) / 'driver.cpp'
        source.write_text(DRIVER)
        cls.exe = Path(cls.tmp.name) / 'driver'
        subprocess.run([compiler, '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(SANITY.parent),
                        str(source), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_driver(self, samples):
        text = '\n'.join(' '.join(f'{x:.9g}' for x in p + v) for p, v in samples) + '\n'
        out = subprocess.run([str(self.exe)], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
        rows = []
        for line in out:
            shape, finite, aspect, dot, length, t_len, changed = line.split()
            rows.append(dict(shape=shape, finite=int(finite), aspect=float(aspect), dot=float(dot), length=float(length),
                             t_len=float(t_len), changed=int(changed)))
        return rows

    def test_perspective_camera_reads_as_persp_with_the_display_aspect(self):
        p = perspective()
        v1 = view(0.3, -0.2, (1200.0, -300.0, 4000.0))
        v2 = view(0.31, -0.2, (1210.0, -300.0, 4005.0))
        depth = list(p)
        depth[10], depth[14] = 1.0000019, -6.0000184  # the per-draw depth scratch: not a camera change
        rows = self.run_driver([(p, v1), (p, v1), (depth, v1), (depth, v2)])
        for r in rows:
            self.assertEqual((r['shape'], r['finite']), ('persp', 1))
            self.assertAlmostEqual(r['aspect'], 5120 / 1440, places=4)
            self.assertLess(r['dot'], 1e-6)
            self.assertLess(r['length'], 1e-6)
        self.assertAlmostEqual(rows[0]['t_len'], math.sqrt(1200 ** 2 + 300 ** 2 + 4000 ** 2), places=2)
        self.assertEqual([r['changed'] for r in rows], [0, 0, 0, 1])

    def test_orthographic_skewed_and_garbage_are_told_apart(self):
        identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        skew = view(0.5, 0.1, (0.0, 0.0, 0.0))
        skew[0] *= 1.5  # one stretched basis vector: a length error and off-diagonal dots
        rows = self.run_driver([(orthographic(), identity), (perspective(), skew), ([0.0] * 16, [0.0] * 16),
                                ([float('nan')] + [0.0] * 15, identity)])
        self.assertEqual(rows[0]['shape'], 'ortho')
        self.assertAlmostEqual(rows[0]['aspect'], 5120 / 1440, places=4)
        self.assertEqual((rows[0]['dot'], rows[0]['length'], rows[0]['t_len']), (0.0, 0.0, 0.0))
        self.assertEqual(rows[1]['shape'], 'persp')
        self.assertGreater(rows[1]['length'], 0.1)
        self.assertGreater(rows[1]['dot'], 0.01)
        self.assertEqual((rows[2]['shape'], rows[2]['finite'], rows[2]['aspect']), ('other', 1, 0.0))
        self.assertEqual(rows[2]['length'], 1.0)  # zero rows: |1 - 0|
        self.assertEqual((rows[3]['shape'], rows[3]['finite']), ('other', 0))


class FlCameraWiring(unittest.TestCase):
    def test_edition_lists_and_loader(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        linked = re.search(r'set\(X3M_AP_ENGINE_LINKED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('camera_state.cpp', linked)
        shared = re.search(r'set\(X3M_SHARED_SOURCES\n(.*?)\)\n', cmake, re.S).group(1)
        self.assertNotIn('camera_state', shared)
        self.assertIn('src/engine/fl/executable_identity_fl.cpp src/engine/fl/camera_state_fl.cpp', cmake)
        self.assertIn('src/proxy/camera_state.cpp', re.search(r'set\(X3M_AP_SOURCES\n(.*?)\)\n', cmake, re.S).group(1))
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        fl_block = loader[loader.index('#if X3M_EDITION_FL\n    // FL edition'):]
        fl_block = fl_block[:fl_block.index('#endif')]
        self.assertIn('x3m::camera_state::initialize();', fl_block)

    def test_provider_contract(self):
        text = PROVIDER.read_text()
        code = re.sub(r'//.*', '', text)
        self.assertNotIn('0x400000', code)
        self.assertNotIn('0x5aae', code.lower())  # preferred VAs never appear; RVAs come from the fragment
        self.assertIn('x3m::fl::module_base()', code)
        self.assertIn('#include "camera_state_fl_inc.h"', code)
        for status in ('"disabled"', '"executable_mismatch"', '"anchor_mismatch"', '"slots_unreadable"', '"active"'):
            self.assertIn(status, code)
        self.assertIn('"fl.camera status=%s', code)
        self.assertIn('"fl.camera_sample k=%u status=%s', code)
        self.assertIn('diagnostic.burst = 2;', code)  # two consecutive reads per period: background then scene
        self.assertIn('L"X3M_FL_CAMERA_LOG"', code)
        self.assertIn('QueryPerformanceCounter', code)
        self.assertNotIn('new ', code)
        self.assertNotIn('malloc', code)
        self.assertNotIn('std::string', code)
        self.assertNotIn('fabs', code)  # camera_reprojection.h: the mingw build emits x87 for it
        self.assertNotIn('fabs', re.sub(r'//.*', '', SANITY.read_text()))


if __name__ == '__main__':
    unittest.main()
