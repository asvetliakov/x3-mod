"""Engine effects phase 1a (X3M_ENGINE_EFFECTS; docs/architecture/engine-effects-modern.md sections 1, 2 and 5): the
portable core src/proxy/engine_effects_core.h compiled on the host, the option, the launcher and the wiring.

The harness checks the option words, the recogniser's truth table against a Python twin (every combination of the
effect pair, the shadowed Z-write and blend states, scope, node read, JET flags, mode, call redirects and ring), the throttle formula,
the c4-6 order selection (order a, order b, ambiguous, mismatch, invalid) with origin / axis / size within 1e-5, the
record flags, the engine_bodies.json parser on documents written by tools/effects/engine_bodies.py's own dumps() and
its fail-closed cases, the name -> id resolution over a synthetic engine body table (by id, by scan, a re-bound
dynamic slot), and times the per-draw core (classify + record + ring push). With the installed Mayhem 3 tree the real
generated table is parsed by the C++ core and compared entry for entry with Python's json.
"""
import importlib.util
import json
import math
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'tools/config'))
import bob1  # noqa: E402
import schema  # noqa: E402
from verification.analysis.test_config_schema import hermetic_launcher, launch_env  # noqa: E402

_spec = importlib.util.spec_from_file_location('engine_bodies', ROOT / 'tools/effects/engine_bodies.py')
eb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(eb)

HARNESS = r'''
#include "engine_effects_core.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace x3m::engine_effects::core;
static unsigned failed = 0;
static void expect(bool ok, const char* what) { if (!ok) { ++failed; std::printf("FAIL %s\n", what); } }
static bool near(float a, float b, float tol = 1e-5f) { return std::fabs(a - b) <= tol * (std::fabs(b) > 1.f ? std::fabs(b) : 1.f); }
// A synthetic 32-bit image: the manager at 0x1000 (global at 0xffc), the slot array at 0x2000, names from 0x60000.
static unsigned char memory[0x70000];
static bool reader(std::uintptr_t address, void* out, std::size_t size) {
    if (address < 0x800 || address + size > sizeof memory) return false;
    std::memcpy(out, memory + address, size);
    return true;
}
static void put32(std::uint32_t at, std::uint32_t v) { std::memcpy(memory + at, &v, 4); }
static std::uint32_t name_at = 0x60000;
static std::uint32_t name(const char* text) { const std::uint32_t at = name_at; std::strcpy(reinterpret_cast<char*>(memory + at), text); name_at += 64; return at; }
static std::vector<char> slurp(const char* path) {
    std::vector<char> text;
    if (FILE* f = std::fopen(path, "rb")) { char b[65536]; std::size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) text.insert(text.end(), b, b + n); std::fclose(f); }
    return text;
}
static int parse_file(const char* path) {
    const std::vector<char> text = slurp(path);
    static BodyTable t;
    std::size_t fault = 0;
    const bool ok = parse_body_table(text.data(), text.size(), &t, &fault);
    std::printf("PARSE ok=%d count=%u refused=%u fault=%zu\n", int(ok), t.count, t.refused, fault);
    for (unsigned i = 0; i < t.count; ++i) {
        const Body& b = t.bodies[i];
        std::printf("BODY %s|%d|%u|%s|%u|%.3f|%.3f|%.3f|%.3f|%.3f\n", b.name, b.id, unsigned(b.lists), cluster_name(b.cluster),
                    unsigned(b.extent), double(b.value), double(b.z_min), double(b.z_max), double(b.half_width[0]), double(b.half_width[1]));
    }
    return ok ? 0 : 1;
}
// World rows of a rotation (unit rows r0, r1, r2 = model x, y, z in world) scaled by (k, k, k z) plus a translation, in
// either register order.
static void rows_of(const float r[9], float k, float z, const float t[3], bool order_b, float out[12]) {
    const float X[3] = {r[0] * k, r[1] * k, r[2] * k}, Y[3] = {r[3] * k, r[4] * k, r[5] * k}, Z[3] = {r[6] * k * z, r[7] * k * z, r[8] * k * z};
    for (unsigned i = 0; i < 3; ++i) {
        if (order_b) { const float* axis = i == 0 ? X : i == 1 ? Y : Z; out[i * 4 + 0] = axis[0]; out[i * 4 + 1] = axis[1]; out[i * 4 + 2] = axis[2]; }
        else { out[i * 4 + 0] = X[i]; out[i * 4 + 1] = Y[i]; out[i * 4 + 2] = Z[i]; }
        out[i * 4 + 3] = t[i];
    }
}
static void rotation(float yaw, float pitch, float roll, float r[9]) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
    const float m[9] = {cy * cr + sy * sp * sr, cp * sr, -sy * cr + cy * sp * sr,
                        -cy * sr + sy * sp * cr, cp * cr, sy * sr + cy * sp * cr,
                        sy * cp, -sp, cy * cp};
    std::memcpy(r, m, sizeof m);
}
static const char* DOC_OK =
    "{\n \"bodies\": {\n  \"effects\\\\engines\\\\fx_engine_argon_M3\": {\n   \"cluster\": \"lightblue\",\n   \"half_width\": [\n    1.5,\n    2.25\n   ],\n"
    "   \"id\": null,\n   \"lists\": [\n    \"jet\"\n   ],\n   \"value\": 4170,\n   \"z_extent\": \"negative\",\n   \"z_max\": 0.0,\n   \"z_min\": -1.0\n  },\n"
    "  \"v\\\\00566\": {\n   \"cluster\": \"grey\",\n   \"half_width\": null,\n   \"id\": 566,\n   \"lists\": [\n    \"jet\",\n    \"smalljet\"\n   ],\n"
    "   \"value\": 1e3,\n   \"z_extent\": \"none\",\n   \"z_max\": null,\n   \"z_min\": null\n  },\n"
    "  \"effects\\\\engines\\\\no_colour\": {\"cluster\": null, \"id\": null, \"lists\": [\"jet\"], \"value\": 9366, \"z_extent\": \"both\", \"extra\": {\"a\": [1, {\"b\": true}]}}\n"
    " },\n \"counts\": {\"bodies\": 3},\n \"missing\": [],\n \"rules\": {\"key\": \"v\\\\%05d\"},\n \"schema\": 1\n}\n";
int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "parse")) return parse_file(argv[2]);
    // Option words.
    Mode m = Mode::off;
    expect(parse_mode("native", 6, &m) && m == Mode::native && parse_mode("OFF", 3, &m) && m == Mode::off && parse_mode("Plumes", 6, &m) && m == Mode::plumes, "the three words, case folded");
    expect(!parse_mode("", 0, &m) && !parse_mode("of", 2, &m) && !parse_mode("offf", 4, &m) && !parse_mode("on", 2, &m) && !parse_mode("native ", 7, &m) && !parse_mode("plume", 5, &m), "other words refused");
    expect(!suppresses(Mode::native) && suppresses(Mode::off) && suppresses(Mode::plumes), "plumes suppresses like off");
    expect(effect_pair(0xd5e1c75351ed3f04ull, 0x8360f422de08b5bdull) && effect_pair(0x89193868c61c3846ull, 0x8360f422de08b5bdull) && !effect_pair(0xd5e1c75351ed3f04ull, 1) && !effect_pair(1, 0x8360f422de08b5bdull), "the effects pair");
    // Truth table: bits 0 pair, 1 zwrite_known, 2 zwrite, 3 blend_known, 4 blend, 5 scoped, 6 snapshot, 7-8 flags (0 none,
    // 1 0x4000000 only, 2 0x4000001, 3 0x0000001 only), 9 suppress, 10 ring_full, 11 redirects live.
    for (unsigned bits = 0; bits < (1u << 12); ++bits) {
        DrawState s; Facts f;
        s.pair = bits & 1; s.zwrite_known = bits & 2; s.zwrite = (bits >> 2) & 1; s.blend_known = bits & 8; s.blend = (bits >> 4) & 1;
        f.scoped = bits & 32; f.snapshot = bits & 64;
        const unsigned k = (bits >> 7) & 3;
        f.flags130 = k == 1 ? 0x4000000u : k == 2 ? 0x4000001u | 0x200u : k == 3 ? 1u : 0x80u;
        f.suppress = bits & 512; f.ring_full = bits & 1024; f.redirects = bits & 2048;
        std::printf("TRUTH %u %s\n", bits, verdict_name(classify(s, f)));
    }
    // Throttle: z = scale3 / 65536, s = clamp((z - 0.25) / 1.75).
    const std::uint32_t words[] = {0x28f, 0x4000, 0x10000, 0x1c000, 0x20000, 0x60000, 0x90000};
    for (std::uint32_t w : words) { float z, s; throttle(w, &z, &s); std::printf("THROTTLE %u %.7f %.7f\n", w, double(z), double(s)); }
    // Geometry: a rotated basis at z = 1.7, base size k = 617.25 (value 1234.5 x context 0.5), translation (10, -20, 30).
    float r[9]; rotation(0.7f, -0.4f, 1.1f, r);
    const float t[3] = {10.f, -20.f, 30.f}, k = 617.25f, z = 1.7f;
    float ra[12], rb[12];
    rows_of(r, k, z, t, false, ra);
    rows_of(r, k, z, t, true, rb);
    Geometry g;
    geometry(ra, z, Order::b, &g);
    expect(g.match == Match::a && g.order == Order::a, "order a selected from b pinned");
    expect(near(g.origin[0], 10.f) && near(g.origin[1], -20.f) && near(g.origin[2], 30.f), "origin a");
    expect(near(g.axis[0], -r[6]) && near(g.axis[1], -r[7]) && near(g.axis[2], -r[8]), "axis a = -(model z)");
    expect(near(g.size, k) && near(g.ratio, z), "size and ratio a");
    std::printf("GEOMETRY a match=%s size=%.6f ratio=%.7f axis=%.7f,%.7f,%.7f ratio_b=%.7f\n", match_name(g.match), double(g.size), double(g.ratio), double(g.axis[0]), double(g.axis[1]), double(g.axis[2]), double(g.ratio_b));
    geometry(rb, z, Order::a, &g);
    expect(g.match == Match::b && g.order == Order::b, "order b selected from a pinned");
    expect(near(g.origin[0], 10.f) && near(g.origin[1], -20.f) && near(g.origin[2], 30.f), "origin b");
    expect(near(g.axis[0], -r[6]) && near(g.axis[1], -r[7]) && near(g.axis[2], -r[8]), "axis b = -(model z)");
    expect(near(g.size, k) && near(g.ratio, z), "size and ratio b");
    std::printf("GEOMETRY b match=%s size=%.6f ratio=%.7f ratio_a=%.7f\n", match_name(g.match), double(g.size), double(g.ratio), double(g.ratio_a));
    const float ident[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float ri[12]; rows_of(ident, k, 1.f, t, false, ri);
    geometry(ri, 1.f, Order::b, &g);
    expect(g.match == Match::ambiguous && g.order == Order::b && near(g.axis[2], -1.f), "z = 1 on an aligned basis: ambiguous, pinned order kept");
    geometry(ra, 1.2f, Order::a, &g);
    expect(g.match == Match::mismatch && g.order == Order::a && near(g.size, k), "neither ratio matches: mismatch, pinned order");
    float rn[12]; std::memcpy(rn, ra, sizeof rn); rn[5] = NAN;
    geometry(rn, z, Order::a, &g);
    expect(g.match == Match::invalid, "a NaN row: invalid");
    float rz[12] = {}; geometry(rz, z, Order::a, &g);
    expect(g.match == Match::invalid, "zero rows: invalid");
    // Records.
    static BodyTable bodies; std::size_t fault = 0;
    expect(parse_body_table(DOC_OK, std::strlen(DOC_OK), &bodies, &fault) && bodies.count == 3 && bodies.refused == 0, "the generator's layout parses");
    expect(!std::strcmp(bodies.bodies[0].name, "effects\\engines\\fx_engine_argon_M3") && bodies.bodies[0].id == -1 && bodies.bodies[0].cluster == lightblue &&
           bodies.bodies[0].extent == extent_negative && bodies.bodies[0].value == 4170.f && bodies.bodies[0].half_width[1] == 2.25f && bodies.bodies[0].z_min == -1.f, "named body fields");
    expect(!std::strcmp(bodies.bodies[1].name, "v\\00566") && bodies.bodies[1].id == 566 && bodies.bodies[1].lists == (list_jet | list_smalljet) && bodies.bodies[1].cluster == grey && bodies.bodies[1].value == 1000.f, "v\\00566 fields");
    expect(bodies.bodies[2].cluster == default_cluster && bodies.bodies[2].extent == extent_both, "null cluster: the default cluster");
    Order pinned = Order::a; Record rec; FrameCounts counts;
    RecordInput in; in.scale3 = std::uint32_t(1.7f * 65536.f + .5f); in.model = 20004; in.node_handle = 0x1234; in.serial = 77; in.rows = rb; in.pair = true; in.additive = true; in.body = 0; in.entry = &bodies.bodies[0]; in.frame = 0x100000005ull;
    fill_record(in, &pinned, &rec, &g, &counts);
    expect(pinned == Order::b && counts.match[1] == 1 && (rec.flags & flag_order_b) && (rec.flags & flag_serial) && (rec.flags & flag_effect_pair) && (rec.flags & flag_additive) &&
           !(rec.flags & (flag_unknown_body | flag_steering | flag_brake | flag_rows_unknown)) && (rec.flags >> cluster_shift) == lightblue, "record flags (named body, order b)");
    expect(rec.serial == 77 && rec.model == 20004 && rec.node_handle == 0x1234 && rec.body == 0 && rec.frame == 5 && near(rec.size, k) && near(rec.s, (1.7f - .25f) / 1.75f, 1e-4f), "record fields");
    in.model = 566; in.entry = nullptr; in.body = -1; in.rows = nullptr; in.scale3 = 0x60000; in.serial = 0; in.pair = false; in.additive = false;
    fill_record(in, &pinned, &rec, &g, &counts);
    expect((rec.flags & flag_steering) && (rec.flags & flag_unknown_body) && (rec.flags & flag_rows_unknown) && !(rec.flags & flag_brake) && rec.body == -1 &&
           (rec.flags >> cluster_shift) == default_cluster && rec.size == 0.f && rec.s == 1.f && rec.z == 6.f && counts.rows_unknown == 1 && pinned == Order::b, "v/00566 by model, unknown body, no rows");
    in.model = 1; in.scale3 = 0x60000;
    fill_record(in, &pinned, &rec, &g, &counts);
    expect((rec.flags & flag_brake) && !(rec.flags & flag_steering), "a main jet above 2.0: brake");
    // Parser fail-closed cases: each leaves count 0.
    const char* bad[] = {"", "{", "[]", "{\"schema\": 2, \"bodies\": {}}", "{\"schema\": 1}", "{\"bodies\": {}}",
                         "{\"schema\": 1, \"bodies\": {}} x", "{\"schema\": 1, \"bodies\": {\"a\": {\"value\": }}}",
                         "{\"schema\": 1, \"bodies\": {\"a\": {\"lists\": [\"jet\",]}}}", "{\"schema\": 1, \"bodies\": {\"a\": {\"id\": 5}, }}",
                         "{\"schema\": 1, \"bodies\": {\"a\\q\": {}}}", "{\"schema\": 1, \"bodies\": {\"a\": {\"value\": 1e99}}}"};
    for (const char* doc : bad) {
        static BodyTable scratch; scratch.count = 7;
        const bool ok = parse_body_table(doc, std::strlen(doc), &scratch, &fault);
        std::printf("BAD %d %u\n", int(ok), scratch.count);
        expect(!ok && scratch.count == 0, doc);
    }
    {
        static BodyTable scratch;
        std::string longname = "{\"schema\": 1, \"bodies\": {\"" + std::string(120, 'x') + "\": {}, \"ok\": {\"value\": 3}, \"tab\\tname\": {}, \"\\u0041b\": {}}}";
        expect(parse_body_table(longname.data(), longname.size(), &scratch, &fault) && scratch.count == 2 && scratch.refused == 2 && !std::strcmp(scratch.bodies[1].name, "Ab"), "a long or control-character name is refused, the rest kept");
        static BodyTable empty;
        expect(parse_body_table("{\"schema\":1,\"bodies\":{}}", 24, &empty, &fault) && empty.count == 0, "an empty table is valid");
    }
    // Names -> ids: three fixed-form names by id, two named bodies by scan (one in a fixed slot), one absent.
    static BodyTable table; table.count = 0;
    const char* names[] = {"v\\00566", "v\\00011", "effects\\engines\\fx_a", "Effects\\Engines\\FX_B", "effects\\engines\\absent", "v\\00012"};
    for (const char* n : names) { Body b{}; std::strcpy(b.name, n); b.id = -1; b.cluster = default_cluster; table.bodies[table.count++] = b; }
    static NameIndex index; index.build(table);
    expect(index.find(table, "EFFECTS\\engines\\fx_a") == 2 && index.find(table, "effects/engines/fx_a") == -1 && index.find(table, "effects\\engines\\fx_b") == 3, "index: folded case, separators distinct");
    const std::uint32_t fixed = 11000, dynamic = 3, slots = 0x2000;
    put32(0x1000 + 0xb4, fixed); put32(0x1000 + 0xb8, dynamic); put32(0x1000 + 0xbc, slots); put32(0x0ffc, 0x1000);
    put32(slots + 12 * 0x1c + 0x0c, name("ships\\something")); // v\00012's fixed slot carries another name: not ours
    put32(slots + 700 * 0x1c + 0x0c, name("effects\\engines\\FX_A"));
    put32(slots + (fixed + 0) * 0x1c + 0x0c, name("effects\\engines\\fx_b"));
    put32(slots + (fixed + 2) * 0x1c + 0x0c, name("effects\\engines\\other"));
    const auto et = x3m::lens_flare_cull::core::read_table(&reader, 0x0ffc);
    static Resolver res; res.clear();
    resolve(&reader, et, table, index, &res, 0, fixed + dynamic);
    expect(et.valid && res.resolved == 4 && res.mapped == 4 && res.dynamic_count == 1, "566, 11 by id; fx_a (fixed 700), fx_b (dynamic 20000) by scan");
    expect(res.entry(566) == 0 && res.entry(11) == 1 && res.entry(700) == 2 && res.entry(20000) == 3 && res.entry(12) == -1 && res.entry(20002) == -1 && res.entry(0x9000) == -1, "id -> entry");
    expect(mappings_hold(&reader, et, table, &res, 32), "mappings hold");
    put32(slots + (fixed + 0) * 0x1c + 0x0c, name("ships\\y"));
    expect(!mappings_hold(&reader, et, table, &res, 32), "a re-bound dynamic slot no longer holds");
    // Per-draw cost of the core: classify + fill_record (geometry from c4-6, four square roots) + ring push.
    static Ring ring; ring.clear();
    DrawState s; s.pair = true; s.zwrite_known = true; s.zwrite = 0; s.blend_known = true; s.blend = 1;
    Facts f; f.scoped = true; f.snapshot = true; f.flags130 = jet_flags; f.suppress = true; f.redirects = true;
    volatile std::uint32_t jitter = 0x1b333;
    const unsigned iterations = 4000000;
    float sink = 0.f; unsigned suppressed = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        if (ring.full()) ring.clear();
        f.ring_full = ring.full();
        const Verdict v = classify(s, f);
        if (v != Verdict::suppressed) continue;
        RecordInput ri; ri.scale3 = jitter + (i & 1023); ri.model = 566 + (i & 7); ri.node_handle = i; ri.serial = i; ri.rows = ra; ri.pair = true;
        ri.body = int(i & 3); ri.entry = &bodies.bodies[i % 3]; ri.frame = i;
        Record* out = ring.push();
        fill_record(ri, &pinned, out, &g, &counts);
        sink += out->size + out->s;
        ++suppressed;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iterations;
    std::printf("BENCH iterations=%u suppressed=%u ns_per_draw=%.2f sink=%.3f\n", iterations, suppressed, ns, double(sink));
    std::printf("engine_effects_core checks_failed=%u\n", failed);
    return failed ? 1 : 0;
}
'''

VERDICTS = ('none', 'not_jet', 'suppressed', 'forwarded_unscoped', 'forwarded_snapshot', 'forwarded_opaque', 'forwarded_state',
            'forwarded_overflow', 'forwarded_native', 'forwarded_patch_missing')


def twin(bits):
    """The recogniser's truth table, written independently of the header (engine-effects-modern.md section 1)."""
    pair, zk, zw, bk, bl = bits & 1, bits & 2, (bits >> 2) & 1, bits & 8, (bits >> 4) & 1
    scoped, snapshot, kind, suppress, full, redirects = bits & 32, bits & 64, (bits >> 7) & 3, bits & 512, bits & 1024, bits & 2048
    flags = {0: 0x80, 1: 0x4000000, 2: 0x4000201, 3: 1}[kind]
    if not (pair or (bk and bl and zk and not zw)):
        return 'none'
    if not scoped:
        return 'forwarded_unscoped' if pair else 'none'
    if not snapshot:
        return 'forwarded_snapshot'
    if flags & 0x4000001 != 0x4000001:
        return 'not_jet'
    if not zk:
        return 'forwarded_state'
    if zw:
        return 'forwarded_opaque'
    if not suppress:
        return 'forwarded_native'
    if not redirects:  # off|plumes arm only with both call redirects live (engine_effects_patch::installed())
        return 'forwarded_patch_missing'
    return 'forwarded_overflow' if full else 'suppressed'


def build(directory):
    compiler = shutil.which('clang++') or shutil.which('c++')
    if compiler is None:
        raise unittest.SkipTest('A host C++ compiler is required')
    (directory / 'harness.cpp').write_text(HARNESS)
    executable = directory / 'engine_effects_host'
    run = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                          str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
    if run.returncode:
        raise AssertionError(run.stdout + run.stderr)
    return executable


class EngineEffectsCore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='x3-engine-effects-host-')
        cls.directory = Path(cls.temporary.name)
        cls.executable = build(cls.directory)
        run = subprocess.run([str(cls.executable)], capture_output=True, text=True, timeout=120)
        cls.returncode, cls.lines = run.returncode, run.stdout.splitlines()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def rows(self, tag):
        return [line.split()[1:] for line in self.lines if line.startswith(tag + ' ')]

    def test_checks_pass(self):
        self.assertEqual(self.returncode, 0, '\n'.join(l for l in self.lines if l.startswith('FAIL')))
        self.assertEqual(self.lines[-1], 'engine_effects_core checks_failed=0')

    def test_truth_table_matches_the_twin(self):
        rows = self.rows('TRUTH')
        self.assertEqual(len(rows), 4096)
        table = {int(b): v for b, v in rows}
        self.assertEqual(table, {b: twin(b) for b in range(4096)})
        self.assertEqual(set(table.values()), set(VERDICTS))  # every verdict is reachable

    def test_throttle(self):
        for word, z, s in self.rows('THROTTLE'):
            expected_z = int(word) / 65536
            self.assertAlmostEqual(float(z), expected_z, places=6)
            self.assertAlmostEqual(float(s), min(1.0, max(0.0, (expected_z - 0.25) / 1.75)), places=6)
        self.assertEqual(len(self.rows('THROTTLE')), 7)

    def test_order_selection(self):
        rows = {r[0]: dict(f.split('=') for f in r[1:]) for r in self.rows('GEOMETRY')}
        self.assertEqual((rows['a']['match'], rows['b']['match']), ('a', 'b'))
        for order in 'ab':
            self.assertAlmostEqual(float(rows[order]['size']), 617.25, delta=617.25e-5)
            self.assertAlmostEqual(float(rows[order]['ratio']), 1.7, delta=1e-5)
        # The other order's ratio is far from z on the rotated basis, so the choice is decisive.
        self.assertGreater(abs(float(rows['a']['ratio_b']) - 1.7), 0.01)
        self.assertGreater(abs(float(rows['b']['ratio_a']) - 1.7), 0.01)

    def test_parser_fail_closed(self):
        rows = self.rows('BAD')
        self.assertEqual(len(rows), 12)
        self.assertTrue(all(r == ['0', '0'] for r in rows), rows)

    def test_generator_output_parses(self):
        """A table written by engine_bodies.dumps (indent 1, sorted keys, escaped backslashes) reads back entry for entry."""
        table = dict(schema=eb.SCHEMA, tool='tools/effects/engine_bodies.py', generated_from={'root': '<game-root>'},
                     rules=dict(clusters={k: eb.hexrgb(v) for k, v in eb.CLUSTERS.items()}, key='v\\%05d'),
                     counts=dict(bodies=2, missing=1),
                     bodies={'effects\\engines\\fx_engine_xtc_red_big2': dict(id=None, lists=['jet'], value=9366, z_min=-1.0, z_max=1.0,
                                                                          z_extent='both', half_width=[0.25, 0.125], cluster='red', tier='big2',
                                                                          mean_linear=[0.5, 0.1, 0.1], blend=dict(src=2, dst=4, law='ONE/INVSRCCOLOR')),
                             'v\\00566': dict(id=566, lists=['jet', 'smalljet'], value=520, z_min=-1.0, z_max=0.0, z_extent='negative',
                                              half_width=[0.5, 0.5], cluster='grey', tier='tiny', mean_linear=None, blend=None)},
                     missing=[dict(name='effects\\engines\\gone', id=None, lists=['jet'], reason='not_found')])
        path = self.directory / 'engine_bodies.json'
        path.write_text(eb.dumps(table))
        run = subprocess.run([str(self.executable), 'parse', str(path)], capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, run.stdout)
        lines = run.stdout.splitlines()
        self.assertEqual(lines[0], 'PARSE ok=1 count=2 refused=0 fault=0')
        self.assertEqual(lines[1:], ['BODY effects\\engines\\fx_engine_xtc_red_big2|-1|1|red|3|9366.000|-1.000|1.000|0.250|0.125',
                                     'BODY v\\00566|566|3|grey|1|520.000|-1.000|0.000|0.500|0.500'])

    def test_per_draw_cost(self):
        bench = dict(f.split('=') for f in self.rows('BENCH')[0])
        self.assertEqual(int(bench['suppressed']), int(bench['iterations']))
        self.assertLess(float(bench['ns_per_draw']), 300.0)  # the design's few-hundred-ns budget (host core only)


@unittest.skipUnless((bob1.DEFAULT_GAME / 'addon/06.cat').exists(), 'installed game tree unavailable')
class EngineEffectsInstalledTable(unittest.TestCase):
    def test_generated_table_parses_entry_for_entry(self):
        table = eb.generate(bob1.DEFAULT_GAME)
        with tempfile.TemporaryDirectory(prefix='x3-engine-effects-table-') as temporary:
            directory = Path(temporary)
            executable = build(directory)
            path = directory / 'engine_bodies.json'
            path.write_text(eb.dumps(table))
            run = subprocess.run([str(executable), 'parse', str(path)], capture_output=True, text=True, timeout=60)
        lines = run.stdout.splitlines()
        self.assertEqual(run.returncode, 0, lines[:2])
        self.assertEqual(lines[0], f'PARSE ok=1 count={len(table["bodies"])} refused=0 fault=0')
        parsed = {l[5:].split('|')[0]: l[5:].split('|')[1:] for l in lines[1:]}
        extents = {'none': '0', 'negative': '1', 'positive': '2', 'both': '3'}
        for name, b in table['bodies'].items():
            row = parsed[name]
            self.assertEqual(int(row[0]), b['id'] if b['id'] is not None else -1, name)
            self.assertEqual(int(row[1]), ('jet' in b['lists']) | 2 * ('smalljet' in b['lists']), name)
            self.assertEqual(row[2], b['cluster'] or 'white', name)
            self.assertEqual(row[3], extents[b['z_extent']], name)
            self.assertAlmostEqual(float(row[4]), b['value'], delta=abs(b['value']) * 1e-6 + 1e-3)


class EngineEffectsOption(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['engine_effects']
        # The launcher sends it only when given: no schema default, the DLL's built-in native.
        self.assertEqual((e['env'], e['type'], e['section'], e['default'], e['builtin'], e['choices'], e['launcher'], e['developer']),
                         ('X3M_ENGINE_EFFECTS', 'enum', 'engine', None, 'native', ('native', 'off', 'plumes'), '--engine-effects', False))
        self.assertTrue(schema.BY_KEY['engine_bodies']['developer'])
        table = (ROOT / 'src/config/config_schema_inc.h').read_text()
        self.assertIn('{"X3M_ENGINE_EFFECTS", "engine_effects", Type::Enum, nullptr,', table)
        self.assertIn(';engine_effects = native', (ROOT / 'assets/x3m.ini').read_text())

    def test_launcher(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            self.assertNotIn('X3M_ENGINE_EFFECTS', launch_env(module, game, wine))  # the default flight sends nothing: native
            self.assertEqual(launch_env(module, game, wine, '--engine-effects', 'off')['X3M_ENGINE_EFFECTS'], 'off')
            self.assertEqual(launch_env(module, game, wine, '--config', '--engine-effects', 'plumes')['X3M_ENGINE_EFFECTS'], 'plumes')
            self.assertNotIn('X3M_ENGINE_EFFECTS', launch_env(module, game, wine, '--config'))
            self.assertNotIn('X3M_ENGINE_EFFECTS', launch_env(module, game, wine, '--vanilla'))
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--engine-effects', 'off')

    def test_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('engine_effects::initialize();'), capture.index('lod_occlusion::initialize();'))
        self.assertIn('configure_engine_effects(engine_effects::hook_wanted(), engine_effects::suppress(),', capture)
        self.assertLess(capture.index('engine_effects::begin_frame(ctx.frame);'), capture.index('ctx.motion_output.begin_frame(ctx.frame, ctx.capture);'))
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        draw = motion[motion.index('MotionRoute MotionOutput::before_draw('):]
        self.assertLess(draw.index('if (engine_hook_ && engine_effects_draw(call, route)) return route;'), draw.index('route.evaluated = true;'))
        self.assertIn('#include "motion_output_engine_effects_inc.h"', motion)
        inc = (ROOT / 'src/proxy/motion_output_engine_effects_inc.h').read_text()
        self.assertIn('route.submit = false;', inc)
        self.assertIn('route.submission_error = D3D_OK;', inc)
        self.assertIn('src/proxy/engine_effects.cpp', (ROOT / 'CMakeLists.txt').read_text())
        self.assertIn('../../src/proxy/engine_effects.cpp', (ROOT / 'verification/probe/build_motion_output.sh').read_text())
        module = (ROOT / 'src/proxy/engine_effects.cpp').read_text()
        self.assertIn('object_trace::executable_verified()', module)
        self.assertIn('return engine_effects_patch::installed();', module)  # the suppression's arming signal
        self.assertEqual(len(re.findall(r'config::get\(L"X3M_ENGINE_EFFECTS"', module)), 1)  # read once, at initialize


if __name__ == '__main__':
    unittest.main()
