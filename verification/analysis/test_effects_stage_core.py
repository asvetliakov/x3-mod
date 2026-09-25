"""Host checks of the effects stage core (src/proxy/effects_stage_core.h,
docs/architecture/effects-modernisation-opus.md sections 2, 3.1, 3.3, 8 and 9).

The core compiled from its header on the host: the sparse texture key against
the Python mirror of tools/effects/effect_keys.py (same bytes, same key; a byte
inside a run changes it, a byte outside every run does not); the key table
parser on the generated effect_keys.json and on malformed documents; bolt
matching on synthetic streams (velocity within 1 %, the ambiguity refusal at a
spacing below 2 |v|, the cut reset); the shell association among three boxes
(the right owner, the sphere fallback, the decal beyond 1.5 radii) and the
four-slot policy; the arming window; the node-age map; the object -> world
rows from clip rows; the instance derivation and the vertex layout. Then the
--effects-stage launcher gates (--dry-run only, never a launch). No Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'src/proxy/effects_stage_core.h'
TABLE = ROOT / 'tools/effects/effect_keys.json'
sys.path.insert(0, str(ROOT / 'tools/effects'))
import effect_keys  # noqa: E402

HARNESS = r'''
#include "effects_stage_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace x3m::effects_stage;
static void key_case(const char* path) {
    std::ifstream in(path, std::ios::binary); std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    // 64 x 64 DXT5: 16 block rows of 256 bytes.
    std::printf("KEY %016llx\n", (unsigned long long)sparse_key(64, 64, fmt_dxt5, 16, 256, bytes.data(), 256));
    std::vector<unsigned char> padded(16 * 320); for (unsigned r = 0; r < 16; ++r) std::memcpy(padded.data() + r * 320, bytes.data() + r * 256, 256);
    std::printf("KEY_PITCH %016llx\n", (unsigned long long)sparse_key(64, 64, fmt_dxt5, 16, 256, padded.data(), 320));
    std::uint32_t rows = 0, rb = 0; std::printf("LAYOUT dxt1=%u,%u a8=%u,%u bad=%u\n", (level0_layout(130, 70, fmt_dxt1, &rows, &rb) ? rows : 0), rb, (level0_layout(7, 5, fmt_a8r8g8b8, &rows, &rb) ? rows : 0), rb, unsigned(level0_layout(8, 8, 999, &rows, &rb)));
}
static void table_case(const char* path) {
    std::ifstream in(path, std::ios::binary); std::string text((std::istreambuf_iterator<char>(in)), {});
    KeyTable t; std::size_t fault = 0; const bool ok = parse_key_table(text.data(), text.size(), &t, &fault);
    std::printf("TABLE ok=%u count=%u dup=%u refused=%u\n", unsigned(ok), t.count, t.duplicates, t.refused);
    for (unsigned i = 0; i < t.count; ++i) std::printf("ENTRY %016llx %s %s %.6f,%.6f,%.6f %.4f,%.4f,%.4f\n", (unsigned long long)t.entries[i].key, class_name(t.entries[i].cls), t.entries[i].name, t.entries[i].extent[0], t.entries[i].extent[1], t.entries[i].extent[2], t.entries[i].tint[0], t.entries[i].tint[1], t.entries[i].tint[2]);
    const char* bad[] = {"{\"schema\":1,\"entries\":[{\"key\":\"00112233445566778\",\"class\":\"shield_hit\"}]}", "{\"schema\":2,\"entries\":[]}", "{\"schema\":1,\"entries\":[{\"key\":\"0011223344556677\",\"class\":\"shield_hit\"},{\"key\":\"0011223344556678\",\"class\":\"bolt\"", "[]", "{\"schema\":1,\"entries\":[{\"key\":\"0011223344556677\",\"class\":\"nothing\"},{\"key\":\"0011223344556677\",\"class\":\"bolt\"},{\"key\":\"0011223344556677\",\"class\":\"bolt\",\"extra\":{\"a\":[1,2,{\"b\":3}]}}]}", "{\"schema\":1,\"entries\":[{\"key\":\"0011223344556677\",\"class\":\"bolt\"}]} x"};
    for (const char* b : bad) { KeyTable x; std::size_t f = 0; const bool r = parse_key_table(b, std::strlen(b), &x, &f); std::printf("BAD ok=%u count=%u dup=%u refused=%u fault=%u\n", unsigned(r), x.count, x.duplicates, x.refused, unsigned(f)); }
}
static void stream(BoltInstance* out, unsigned n, float spacing, const float* axis, float offset, float perp) {
    for (unsigned i = 0; i < n; ++i) { out[i] = BoltInstance{}; for (unsigned k = 0; k < 3; ++k) { out[i].axis[k] = axis[k]; out[i].centre[k] = axis[k] * (offset + spacing * float(i)); } out[i].centre[1] += perp * float(i % 2); out[i].period = 24; out[i].half_length = 40.f; out[i].half_width = 4.f; }
}
static void match_case(float spacing, float speed, bool cut, const char* label) {
    const float axis[3] = {0.6f, 0.0f, 0.8f}; BoltInstance prev[8], cur[8];
    stream(prev, 8, spacing, axis, 1000.f, 3.f); stream(cur, 8, spacing, axis, 1000.f + speed, 3.f);
    MatchStats s; match_instances(prev, 8, cur, 8, default_match_s_max, default_match_eps, cut, &s);
    float err = 0.f; for (unsigned i = 0; i < 8; ++i) if (cur[i].matched) { const float v = std::sqrt(cur[i].velocity[0] * cur[i].velocity[0] + cur[i].velocity[1] * cur[i].velocity[1] + cur[i].velocity[2] * cur[i].velocity[2]); const float e = std::fabs(v - speed) / speed; if (e > err) err = e; }
    std::printf("MATCH %s matched=%u ambiguous=%u none=%u cut=%u median=%.3f err=%.5f\n", label, s.matched, s.refused_ambiguous, s.refused_none, s.refused_cut, s.median_speed, err);
}
static void box(OwnerBox& b, std::uint64_t id, float cx, float cy, float cz, float hx, float hy, float hz, float yaw) {
    b = OwnerBox{}; b.identity = id; b.rows_valid = true; b.half[0] = hx; b.half[1] = hy; b.half[2] = hz;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float r[12] = {c, 0, -s, cx, 0, 1, 0, cy, s, 0, c, cz}; std::memcpy(b.rows, r, sizeof r);
}
static void associate_case() {
    OwnerBox boxes[3]; box(boxes[0], 11, 0, 0, 0, 100, 50, 200, 0.f); box(boxes[1], 22, 5000, 0, 0, 300, 100, 600, 0.7f); box(boxes[2], 33, -3000, 500, 0, 50, 50, 50, 0.f);
    const float p1[3] = {5000 + 300 * 1.2f * std::cos(0.7f) * 0.9f, 0, 300 * 1.2f * std::sin(0.7f) * 0.9f}; // near box 1's x axis rim
    Association a = associate_hit(p1, boxes, 3); std::printf("ASSOC near kind=%u index=%d distance=%.3f local=%.3f,%.3f,%.3f\n", unsigned(a.kind), a.index, a.distance, a.local[0], a.local[1], a.local[2]);
    const float p2[3] = {-3000 + 50 * 1.2f * 1.4f, 500, 0}; a = associate_hit(p2, boxes, 3); std::printf("ASSOC edge kind=%u index=%d distance=%.3f\n", unsigned(a.kind), a.index, a.distance);
    const float p3[3] = {-3000 + 50 * 1.2f * 1.6f, 500, 0}; a = associate_hit(p3, boxes, 3); std::printf("ASSOC far kind=%u index=%d distance=%.3f\n", unsigned(a.kind), a.index, a.distance);
    OwnerBox sphere = OwnerBox{}; sphere.identity = 44; sphere.radius = 100.f; sphere.rows[3] = 9000; const float p4[3] = {9000 + 100 * 1.2f * 1.3f, 0, 0};
    a = associate_hit(p4, &sphere, 1); std::printf("ASSOC sphere kind=%u index=%d distance=%.3f\n", unsigned(a.kind), a.index, a.distance);
    a = associate_hit(p4, nullptr, 0); std::printf("ASSOC none kind=%u index=%d\n", unsigned(a.kind), a.index);
    ShipHits ship{}; ship.owner = 22; const float dirs[5][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}};
    unsigned slots[6]; for (unsigned i = 0; i < 5; ++i) slots[i] = ship.insert(100 + i, dirs[i], 10 + i); slots[5] = ship.insert(101, dirs[0], 99);
    std::printf("SLOTS %u,%u,%u,%u,%u,%u live=%u first0=%llu\n", slots[0], slots[1], slots[2], slots[3], slots[4], slots[5], ship.live(), (unsigned long long)ship.slots[0].first_frame);
    ship.expire(50, 30); std::printf("EXPIRE live=%u\n", ship.live());
    ShellInstance inst; std::printf("SHELL ok=%u centre=%.1f axes=%.3f,%.3f,%.3f\n", unsigned(shell_from_box(boxes[1], &inst)), inst.centre[0], inst.axes[0], inst.axes[1], inst.axes[2]);
}
static void arming_case() {
    Arming a; std::printf("ARM initial=%u", unsigned(a.armed(10, true, true, true))); a.fail(10);
    std::printf(" after_fail=%u at63=%u at64=%u no_caps=%u failures=%u\n", unsigned(a.armed(11, true, true, true)), unsigned(a.armed(73, true, true, true)), unsigned(a.armed(74, true, true, true)), unsigned(a.armed(200, true, false, true)), a.failures);
    AgeMap m; const std::uint64_t f1 = m.touch(7, 1, 100), f2 = m.touch(7, 1, 105), f3 = m.touch(8, 1, 105), f4 = m.touch(7, 1, 105 + age_expire_frames + 200);
    std::printf("AGES %llu %llu %llu %llu used=%u\n", (unsigned long long)f1, (unsigned long long)f2, (unsigned long long)f3, (unsigned long long)f4, m.used);
}
static void clip_case() {
    // W: yaw 0.4 about y plus a translation; V: the camera latch's rows (view = R world + t); P: m00, m11, m20, m21.
    const float yaw = 0.4f, c = std::cos(yaw), s = std::sin(yaw); const float W[12] = {c, 0, -s, 120, 0, 1, 0, -40, s, 0, c, 9000};
    const float pitch = -0.2f, cp = std::cos(pitch), sp = std::sin(pitch); const float R[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp}; const float t[3] = {5, -3, 200};
    const float m00 = 1.2f, m11 = 2.1f, m20 = 0.01f, m21 = -0.02f;
    // The camera latch (camera_reprojection.h): r[i*3+j] = V[i][j] of the D3D view matrix, view_j = sum_i world_i r[i*3+j] + t[j];
    // the effects programs take row_j = (r[j], r[3+j], r[6+j], t[j]).
    float view_rows[12]; for (unsigned j = 0; j < 3; ++j) { view_rows[j * 4] = R[j]; view_rows[j * 4 + 1] = R[3 + j]; view_rows[j * 4 + 2] = R[6 + j]; view_rows[j * 4 + 3] = t[j]; }
    // object -> view rows = view_rows * W (3x4 * 4x4).
    float ov[12]; for (unsigned j = 0; j < 3; ++j) { for (unsigned i = 0; i < 3; ++i) ov[j * 4 + i] = view_rows[j * 4] * W[i] + view_rows[j * 4 + 1] * W[4 + i] + view_rows[j * 4 + 2] * W[8 + i]; ov[j * 4 + 3] = view_rows[j * 4] * W[3] + view_rows[j * 4 + 1] * W[7] + view_rows[j * 4 + 2] * W[11] + view_rows[j * 4 + 3]; }
    float clip[16]; for (unsigned i = 0; i < 4; ++i) { clip[i] = m00 * ov[i] + m20 * ov[8 + i]; clip[4 + i] = m11 * ov[4 + i] + m21 * ov[8 + i]; clip[8 + i] = ov[8 + i]; clip[12 + i] = ov[8 + i]; }
    // The world-from-view basis wv[k*3+j] = (R^-1)[j][k] (world_k = sum_j wv[k*3+j] (view_j - t_j)); R here is orthonormal, so (R^-1)[j][k] = R[k][j] = R[k*3+j].
    float wv[9]; for (unsigned k = 0; k < 3; ++k) for (unsigned j = 0; j < 3; ++j) wv[k * 3 + j] = R[k * 3 + j];
    float out[12]; const bool ok = object_to_world_from_clip(clip, m00, m11, m20, m21, wv, t, out);
    float err = 0.f; for (unsigned i = 0; i < 12; ++i) err = std::max(err, std::fabs(out[i] - W[i]));
    std::printf("CLIP ok=%u err=%.5f\n", unsigned(ok), err);
}
static void derive_case() {
    // One crossed-card body of 24 vertices along +z: two quads of 2 triangles in the xz and yz planes, length 60, width 6, plus a second instance translated.
    std::vector<float> pos; std::vector<std::uint32_t> extras;
    auto add = [&](float x, float y, float z, float u, float v, std::uint32_t colour, float ox, float oy, float oz) { pos.push_back(x + ox); pos.push_back(y + oy); pos.push_back(z + oz); std::uint32_t uw, vw; std::memcpy(&uw, &u, 4); std::memcpy(&vw, &v, 4); extras.push_back(uw); extras.push_back(vw); extras.push_back(colour); };
    for (unsigned inst = 0; inst < 2; ++inst) { const float ox = inst ? 500.f : 0.f, oz = inst ? 900.f : 0.f;
        const float quads[2][4][3] = {{{-3, 0, -30}, {3, 0, -30}, {3, 0, 30}, {-3, 0, 30}}, {{0, -3, -30}, {0, 3, -30}, {0, 3, 30}, {0, -3, 30}}};
        for (unsigned q = 0; q < 2; ++q) { const unsigned idx[6] = {0, 1, 2, 0, 2, 3}; for (unsigned k = 0; k < 6; ++k) { const float* p = quads[q][idx[k]]; add(p[0], p[1], p[2], 0.25f + 0.5f * (idx[k] & 1), 0.5f * (idx[k] >> 1), 0x80ffffffu, ox, 0, oz); } }
        for (unsigned q = 0; q < 2; ++q) { const unsigned idx[6] = {0, 1, 2, 0, 2, 3}; for (unsigned k = 0; k < 6; ++k) { const float* p = quads[q][idx[k]]; add(p[0], p[1], p[2], 0.25f + 0.5f * (idx[k] & 1), 0.5f * (idx[k] >> 1), 0x80ffffffu, ox, 0, oz); } }
    }
    BoltInstance out[4]; unsigned refused = 0; const unsigned n = derive_instances(pos.data(), extras.data(), 48, 24, out, 4, &refused);
    std::printf("DERIVE n=%u refused=%u c0=%.2f,%.2f,%.2f c1=%.2f,%.2f,%.2f axis=%.3f,%.3f,%.3f len=%.2f wid=%.2f uv=%.3f,%.3f alpha=%.4f\n", n, refused, out[0].centre[0], out[0].centre[1], out[0].centre[2], out[1].centre[0], out[1].centre[1], out[1].centre[2], std::fabs(out[0].axis[0]), std::fabs(out[0].axis[1]), std::fabs(out[0].axis[2]), out[0].half_length, out[0].half_width, out[0].uv[0], out[0].uv[1], out[0].alpha);
    BoltVertex v[8]; const unsigned written = write_bolt_vertices(out, n, v, 2);
    std::printf("VERTS written=%u size=%u corner=%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f c=%.2f uvx=%.3f\n", written, unsigned(sizeof(BoltVertex)), v[0].corner_x, v[0].corner_y, v[1].corner_x, v[1].corner_y, v[2].corner_x, v[2].corner_y, v[3].corner_x, v[3].corner_y, v[4].centre[0], v[0].uv_x);
    const float origin[3] = {3.1f, -8.6f, 1000.2f}; std::printf("SPATIAL %u %u\n", unsigned(spatial_identity(5, origin, 8.f) == spatial_identity(5, origin, 8.f)), unsigned(spatial_identity(5, origin, 8.f) != spatial_identity(6, origin, 8.f)));
}
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    key_case(argv[1]); table_case(argv[2]);
    match_case(300.f, 17.f, false, "spaced"); match_case(20.f, 17.f, false, "tight"); match_case(300.f, 17.f, true, "cut");
    associate_case(); arming_case(); clip_case(); derive_case();
    return 0;
}
'''


def compile_harness(directory):
    source = Path(directory) / 'harness.cpp'
    source.write_text(HARNESS)
    exe = Path(directory) / 'harness'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-I', str(CORE.parent), str(source), '-o', str(exe)], check=True)
    return exe


def make_image(seed=1):
    rng = random.Random(seed)
    return bytes(rng.randrange(256) for _ in range(16 * 256))


class Core(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.exe = compile_harness(cls.directory.name)
        cls.image = make_image()
        cls.image_path = Path(cls.directory.name) / 'image.bin'
        cls.image_path.write_bytes(cls.image)
        out = subprocess.run([str(cls.exe), str(cls.image_path), str(TABLE)], check=True, capture_output=True, text=True).stdout
        cls.lines = out.splitlines()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def rows(self, prefix):
        return [line.split()[1:] for line in self.lines if line.startswith(prefix + ' ')]

    def fields(self, prefix):
        out = {}
        for line in self.lines:
            if line.startswith(prefix + ' '):
                for item in line.split()[1:]:
                    if '=' in item:
                        k, v = item.split('=', 1)
                        out[k] = v
                return out
        self.fail(prefix)

    def test_key_matches_python_mirror_and_is_pitch_independent(self):
        expected = '%016x' % effect_keys.sparse_key(64, 64, 0x35545844, 16, 256, self.image)
        self.assertEqual(self.rows('KEY')[0][0], expected)
        self.assertEqual(self.rows('KEY_PITCH')[0][0], expected)
        # A byte inside the first run (row 0, its column start) changes the key; one outside every run does not.
        column0 = 0
        changed = bytearray(self.image); changed[column0] ^= 1
        self.assertNotEqual(effect_keys.sparse_key(64, 64, 0x35545844, 16, 256, bytes(changed)), int(expected, 16))
        runs = set()
        for k in range(16):
            row = (k * 16) // 16
            column = ((k * 2654435761) & 0xffffffff) % 1
            runs.update(range(row * 256 + column, row * 256 + column + 256))
        self.assertEqual(len(runs), 16 * 256)  # every byte of a 256-byte row lies in a run at this size
        # A wider image (row_bytes 1024, 4 block rows): bytes outside the 16 runs do not enter the key.
        wide = bytes(random.Random(3).randrange(256) for _ in range(4 * 1024))
        key = effect_keys.sparse_key(256, 16, 0x35545844, 4, 1024, wide)
        covered = set()
        for k in range(16):
            row = (k * 4) // 16
            column = ((k * 2654435761) & 0xffffffff) % (1024 - 256 + 1)
            covered.update(range(row * 1024 + column, row * 1024 + column + 256))
        outside = next(i for i in range(len(wide)) if i not in covered)
        altered = bytearray(wide); altered[outside] ^= 0xff
        self.assertEqual(effect_keys.sparse_key(256, 16, 0x35545844, 4, 1024, bytes(altered)), key)
        inside = min(covered)
        altered = bytearray(wide); altered[inside] ^= 0xff
        self.assertNotEqual(effect_keys.sparse_key(256, 16, 0x35545844, 4, 1024, bytes(altered)), key)
        self.assertEqual(self.fields('LAYOUT'), {'dxt1': '18,264', 'a8': '5,28', 'bad': '0'})

    def test_table_parses_the_generated_json(self):
        table = json.loads(TABLE.read_text())
        keyed = [e for e in table['entries'] if e.get('key')]
        f = self.fields('TABLE')
        self.assertEqual((f['ok'], int(f['count']), f['dup'], f['refused']), ('1', len(keyed), '0', '0'))
        entries = {row[0]: row for row in self.rows('ENTRY')}
        for e in keyed:
            row = entries[e['key']]
            self.assertEqual((row[1], row[2]), (e['class'], e['name']))
            self.assertTrue(all(abs(float(a) - b) < 1e-5 for a, b in zip(row[3].split(','), e['extent'])), row)
            self.assertTrue(all(abs(float(a) - b) < 1e-3 for a, b in zip(row[4].split(','), e['tint'])), row)
        bad = self.rows('BAD')
        # 17-character key: refused (no key); schema 2, a truncated document (one valid entry before the fault), not an
        # object, trailing bytes after the document: every failure keeps nothing (count=0: fail closed); unknown class +
        # duplicate + nested extras: one kept, one refused, one duplicate.
        self.assertEqual([b[0] for b in bad], ['ok=1', 'ok=0', 'ok=0', 'ok=0', 'ok=1', 'ok=0'])
        self.assertEqual(bad[0][1:4], ['count=0', 'dup=0', 'refused=1'])
        for failed in (1, 2, 3, 5):
            self.assertEqual(bad[failed][1:4], ['count=0', 'dup=0', 'refused=0'], bad[failed])
        self.assertEqual(bad[4][1:4], ['count=1', 'dup=1', 'refused=1'])

    def test_bolt_matching(self):
        spaced = self.fields('MATCH spaced'.split()[0])
        rows = {r[0]: dict(item.split('=') for item in r[1:]) for r in self.rows('MATCH')}
        self.assertEqual(rows['spaced']['matched'], '8')
        self.assertLess(float(rows['spaced']['err']), 0.01)
        self.assertAlmostEqual(float(rows['spaced']['median']), 17.0, places=2)
        self.assertEqual(rows['tight']['matched'], '0')
        self.assertEqual(rows['tight']['ambiguous'], '8')
        self.assertEqual((rows['cut']['matched'], rows['cut']['cut']), ('0', '8'))
        del spaced

    def test_shell_association_and_slots(self):
        rows = {r[0]: dict(item.split('=') for item in r[1:]) for r in self.rows('ASSOC')}
        self.assertEqual((rows['near']['kind'], rows['near']['index']), ('1', '1'))
        self.assertLess(float(rows['near']['distance']), 1.0)
        self.assertEqual((rows['edge']['kind'], rows['edge']['index']), ('1', '2'))  # 1.4 radii: still the shell
        self.assertEqual((rows['far']['kind'], rows['far']['index']), ('3', '-1'))   # 1.6 radii: the decal
        self.assertEqual((rows['sphere']['kind'], rows['sphere']['index']), ('2', '0'))
        self.assertEqual(rows['none']['kind'], '3')
        slots = self.rows('SLOTS')[0]
        self.assertEqual(slots[0], '0,1,2,3,0,1')  # the fifth hit takes the oldest slot (0); a repeat keeps its slot
        self.assertEqual(slots[1:], ['live=4', 'first0=14'])
        self.assertEqual(self.rows('EXPIRE')[0], ['live=0'])
        shell = self.fields('SHELL')
        self.assertEqual(shell['ok'], '1')
        self.assertAlmostEqual(float(shell['centre']), 5000.0)
        axes = [float(v) for v in shell['axes'].split(',')]
        self.assertAlmostEqual(math.hypot(*axes), 300 * 1.2, places=2)

    def test_arming_ages_clip_derive(self):
        arm = self.fields('ARM')
        self.assertEqual(arm, {'initial': '1', 'after_fail': '0', 'at63': '0', 'at64': '1', 'no_caps': '0', 'failures': '1'})
        ages = self.rows('AGES')[0]
        self.assertEqual(ages[:4], ['100', '100', '105', '425'])
        clip = self.fields('CLIP')
        self.assertEqual(clip['ok'], '1')
        self.assertLess(float(clip['err']), 2e-3)
        derive = self.fields('DERIVE')
        self.assertEqual((derive['n'], derive['refused']), ('2', '0'))
        self.assertEqual(derive['c0'], '0.00,0.00,0.00')
        self.assertEqual(derive['c1'], '500.00,0.00,900.00')
        axis = [float(v) for v in derive['axis'].split(',')]  # the vertex covariance of the crossed cards (a triangle-list repeats two corners): within 2 degrees of +z
        self.assertGreater(axis[2], 0.999)
        self.assertLess(max(axis[0], axis[1]), 0.03)
        self.assertTrue(29.5 <= float(derive['len']) <= 31.0 and 2.9 <= float(derive['wid']) <= 4.0, derive)  # the extents along that axis
        self.assertAlmostEqual(float(derive['alpha']), 128 / 255, places=3)
        verts = self.fields('VERTS')
        self.assertEqual((verts['written'], verts['size'], verts['corner'], verts['c']), ('2', '64', '-1,-1,1,-1,-1,1,1,1', '500.00'))
        self.assertEqual(self.rows('SPATIAL')[0], ['1', '1'])


def load_manage():
    spec = importlib.util.spec_from_file_location('manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Launcher(unittest.TestCase):
    """--effects-stage gates through --dry-run only (the launch is refused by the mock)."""
    PREREQUISITES = ['--motion-output', '--hdr', '--taa', '--object-trace', '--object-lifetime', '--ownership']

    def launch(self, directory, *args, vanilla=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        if not vanilla:
            (game / 'd3d9.dll').write_bytes(b'proxy')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, {'X3M_EFFECTS_STAGE': '1', 'X3M_EFFECTS_KEYS': 'stale'}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, **kw):
        code, output, error = self.launch(directory, *args, **kw)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_off_by_default_and_inherited_values_cleared(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *self.PREREQUISITES)
            for name in ('X3M_EFFECTS_STAGE', 'X3M_EFFECTS_SHIELDS', 'X3M_EFFECTS_CENSUS', 'X3M_EFFECTS_BOLT_VIEWS', 'X3M_EFFECTS_BOLT_VIEWS_DEFAULT'):
                self.assertNotIn(name, env)

    def test_effects_stage_forwards_its_variables(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *self.PREREQUISITES, '--effects-stage')
            self.assertEqual((env['X3M_EFFECTS_STAGE'], env['X3M_EFFECTS_SHIELDS'], env['X3M_EFFECTS_CENSUS']), ('1', '0', '0'))
            self.assertEqual((env['X3M_EFFECTS_BOLT_VIEWS'], env['X3M_EFFECTS_BOLT_VIEWS_DEFAULT']), ('chase', '1'))
            self.assertEqual(env['X3M_SCREEN_EMISSION_ADDITIVE'], '1.0')  # implied at gain 1
            self.assertTrue(env['X3M_EFFECTS_KEYS'].startswith('Z:\\'))
            self.assertTrue(env['X3M_EFFECTS_KEYS'].endswith('tools\\effects\\effect_keys.json'))
            env = self.env(directory, *self.PREREQUISITES, '--effects-stage', '--effects-shields', '--effects-census', '--effects-bolt-views', 'all', '--screen-emission-additive', '2')
            self.assertEqual((env['X3M_EFFECTS_SHIELDS'], env['X3M_EFFECTS_CENSUS'], env['X3M_EFFECTS_BOLT_VIEWS'], env['X3M_EFFECTS_BOLT_VIEWS_DEFAULT']), ('1', '1', 'all', '0'))
            self.assertEqual(env['X3M_SCREEN_EMISSION_ADDITIVE'], '2.0')
            (Path(directory) / 'game' / 'x3m').mkdir()
            (Path(directory) / 'game' / 'x3m' / 'effect_keys.json').write_text('{}')
            env = self.env(directory, *self.PREREQUISITES, '--effects-stage')
            self.assertNotIn('X3M_EFFECTS_KEYS', env)  # the installed table (the DLL's default path) wins

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (['--effects-stage'], ['--effects-stage', '--motion-output', '--hdr', '--ownership'], ['--effects-shields', *self.PREREQUISITES],
                         ['--effects-stage', *self.PREREQUISITES, '--screen-emission']):
                code, _, error = self.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('effects', error)
            code, _, error = self.launch(directory, '--effects-stage', vanilla=True)
            self.assertEqual(code, 2)
            self.assertIn('--vanilla', error)
            env = self.env(directory, vanilla=True)
            self.assertNotIn('X3M_EFFECTS_STAGE', env)


if __name__ == '__main__':
    unittest.main()
