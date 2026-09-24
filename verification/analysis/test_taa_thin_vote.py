"""Host tests of the TAA thin vote (--taa-thin-vote, X3M_TAA_THIN_VOTE; docs/architecture/taa-thin-geometry-alternatives.md
section 3.2): the per-subset triangle-height histogram and the per-draw [0.5, 3] px window of src/proxy/thin_vote_core.h
(compiled on the host), the launcher option (off by default and forwarded only when given, on needs the route, the ownership
wrapper and the sun-share lane, refused under --vanilla), and the source contract (c216/c217 unchanged, c218 uploaded only
with the option, the plain programs' bytecode unchanged). No game, no Wine."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TAA = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa']
LANE = ['--hdr', '--sun-shadow-lane']

HARNESS = r'''
#include "thin_vote_core.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m::thin_vote;
struct Mesh { std::vector<float> xyz; std::vector<unsigned short> indices; };
// A w x L rectangle in the XY plane at (x, y), two triangles; `z` lifts it (all positions FLOAT3, stride 12).
void quad(Mesh& m, float x, float y, float w, float l, float z = 0.f) {
    const unsigned short base = (unsigned short)(m.xyz.size() / 3);
    const float p[4][3] = {{x, y, z}, {x + w, y, z}, {x, y + l, z}, {x + w, y + l, z}};
    for (auto& v : p) m.xyz.insert(m.xyz.end(), v, v + 3);
    const unsigned short t[6] = {base, (unsigned short)(base + 1), (unsigned short)(base + 2), (unsigned short)(base + 2), (unsigned short)(base + 1), (unsigned short)(base + 3)};
    m.indices.insert(m.indices.end(), t, t + 6);
}
bool measure_mesh(const Mesh& m, Histogram& h) {
    return measure(reinterpret_cast<const unsigned char*>(m.xyz.data()), unsigned(m.xyz.size() / 3), 12, 0, 2, m.indices.data(), false, 0,
                   unsigned(m.indices.size() / 3), h);
}
void print(const char* name, const Histogram& h) {
    std::printf("HIST %s e0=%d total=%u", name, h.e0, h.total);
    for (unsigned b = 0; b <= bins; ++b) std::printf(" c%u=%.6f", b, h.cumulative[b]);
    std::printf("\n");
}
float scale_log2(float px_per_unit) { float rows[16] = {px_per_unit / 128.f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; float s = 0; log2_pixels_per_unit(rows, 256.f, s); return s; }
int main() {
    // Struts: three 0.01 x 1 quads (h = 0.01 units); a panel: one 1 x 1 quad (h = 0.7071).
    Mesh struts, panel, mixed;
    for (int i = 0; i < 3; ++i) { quad(struts, .1f * i, 0, .01f, 1); quad(mixed, .1f * i, 0, .01f, 1); }
    quad(panel, 0, 0, 1, 1); quad(mixed, 2, 0, 1, 1);
    Histogram hs, hp, hm;
    std::printf("MEASURE struts=%d panel=%d mixed=%d\n", measure_mesh(struts, hs), measure_mesh(panel, hp), measure_mesh(mixed, hm));
    print("struts", hs); print("panel", hp); print("mixed", hm);
    // Pixels per unit from rows: |row0.xyz| * W / 2 / w (W = 256): 150 px/unit puts the struts at 1.5 px, 600 at 6 px, 30 at 0.3 px.
    for (float px : {30.f, 60.f, 150.f, 290.f, 600.f}) {
        const float s = scale_log2(px);
        std::printf("FLAG px_per_unit=%.1f log2=%.5f struts=%.6f panel=%.6f mixed=%.6f alpha_struts=%.6f alpha_mixed=%.6f\n", px, s,
                    thin_fraction(hs, s), thin_fraction(hp, s), thin_fraction(hm, s), rt2_alpha(thin_fraction(hs, s)), rt2_alpha(thin_fraction(hm, s)));
    }
    // The bin shift: doubling the scale moves the window one bin down (the fraction at 2s of a histogram equals the fraction
    // at s of the same histogram with e0 + 1).
    Histogram shifted = hm; shifted.e0 += 1;
    std::printf("SHIFT a=%.6f b=%.6f\n", thin_fraction(hm, scale_log2(290.f)), thin_fraction(shifted, scale_log2(145.f)));
    // Log2 of the rows' scale: row0 = (2, 0, 0), w = 4, W = 256: 2 * 256 / 2 / 4 = 64 px/unit.
    { float rows[16] = {2, 0, 0, 7, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 4}; float s = 0; const bool ok = log2_pixels_per_unit(rows, 256.f, s);
      float behind[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1}; float t = 0;
      std::printf("ROWS ok=%d log2=%.5f behind=%d\n", ok, s, log2_pixels_per_unit(behind, 256.f, t)); }
    // The vote threshold: at least half the triangles in the window.
    std::printf("VOTE f40=%.6f f50=%.6f f75=%.6f f100=%.6f\n", rt2_alpha(.4f), rt2_alpha(.5f), rt2_alpha(.75f), rt2_alpha(1.f));
    // FLOAT16_4 positions, INDEX32, the index bias, an index outside the window, a degenerate triangle.
    {
        auto half = [](float f) { unsigned bits; std::memcpy(&bits, &f, 4); const unsigned e = ((bits >> 23) & 255) - 127 + 15, m = (bits >> 13) & 1023;
                                  return (unsigned short)(((bits >> 16) & 0x8000) | (f == 0 ? 0 : (e << 10) | m)); };
        const float q[4][3] = {{0, 0, 0}, {.25f, 0, 0}, {0, 8, 0}, {.25f, 8, 0}};
        unsigned short v[4][4]{}; for (int i = 0; i < 4; ++i) for (int c = 0; c < 3; ++c) v[i][c] = half(q[i][c]);
        const unsigned idx[6] = {10, 11, 12, 12, 11, 13};
        Histogram h; const bool ok = measure(reinterpret_cast<const unsigned char*>(v), 4, 8, 0, 16, idx, true, 10, 2, h);
        const unsigned bad[3] = {10, 11, 14}; Histogram hb; const bool out_of_window = measure(reinterpret_cast<const unsigned char*>(v), 4, 8, 0, 16, bad, true, 10, 1, hb);
        const unsigned flat[6] = {10, 10, 11, 10, 11, 12}; Histogram hf; const bool degenerate = measure(reinterpret_cast<const unsigned char*>(v), 4, 8, 0, 16, flat, true, 10, 2, hf);
        std::printf("HALF ok=%d e0=%d total=%u top=%.6f out_of_window=%d degenerate_ok=%d degenerate_total=%u\n", ok, h.e0, h.total, h.cumulative[bins] - h.cumulative[bins - 1], out_of_window, degenerate, hf.total);
    }
    // The cache: miss, store, find; retry becomes Unreadable after read_attempts; an entry used this frame is not evicted.
    {
        static Cache cache; Key a; a.vb = 1; a.ib = 2; a.primitives = 6; a.stride = 12; a.position_type = 2;
        const bool miss = cache.find(a) == nullptr;
        cache.store(a, &hs); const Entry* e = cache.find(a);
        Key r = a; r.first = 99;
        for (unsigned i = 0; i < read_attempts; ++i) cache.retry(r);
        const Entry* u = cache.find(r);
        Key n = a; n.vb = 3; cache.store(n, nullptr);
        std::printf("CACHE miss=%d known=%d unreadable=%d attempts=%u null_store=%d\n", miss, e && e->state == State::Known, u && u->state == State::Unreadable,
                    u ? u->attempts : 0u, cache.find(n) && cache.find(n)->state == State::Unreadable);
        // Fill one set with ways + 1 keys of the same set (hash % sets equal), all used this frame: the last is refused.
        Cache& c = cache; c.clear(); c.begin_frame();
        std::vector<Key> same; Key k; k.stride = 12; k.position_type = 2;
        for (std::uint64_t vb = 1; same.size() < Cache::ways + 1 && vb < 1000000; ++vb) { k.vb = vb; if (k.hash() % Cache::sets == 7) same.push_back(k); }
        unsigned stored = 0; for (auto& key : same) stored += c.store(key, &hs);
        std::printf("EVICT stored=%u refused=%u\n", stored, c.refused);
        c.begin_frame(); const bool next = c.store(same.back(), &hs);
        std::printf("EVICT_NEXT stored=%d\n", next);
    }
    // Invalidation through the identity index: entries read through wrapper 0x1000 (VB) and 0x2000 (IB), others through
    // 0x3000; invalidating 0x2000 drops exactly its entries; an unwatched store (identity 0) is never dropped; a wrapper
    // with more than per_id entries falls back to a scan and still drops all of them.
    {
        static Cache c; c.clear(); c.begin_frame();
        Key k; k.stride = 12; k.position_type = 2; k.primitives = 6;
        unsigned stored = 0;
        for (unsigned i = 0; i < 4; ++i) { k.vb = 10 + i; stored += c.store(k, &hs, 0x1000, 0x2000); }
        for (unsigned i = 0; i < 3; ++i) { k.vb = 20 + i; stored += c.store(k, &hs, 0x3000, 0); }
        k.vb = 30; stored += c.store(k, nullptr, 0, 0);
        const unsigned dropped_ib = c.invalidate(0x2000), again = c.invalidate(0x2000), vb_after = c.invalidate(0x1000);
        unsigned left = 0; for (unsigned i = 0; i < Cache::size; ++i) left += c.entries[i].state != State::Empty;
        for (unsigned i = 0; i < 10; ++i) { k.vb = 40 + i; stored += c.store(k, &hs, 0x4000, 0); }
        const bool overflowed = c.index.find(0x4000) && c.index.find(0x4000)->overflow;
        const unsigned dropped_many = c.invalidate(0x4000);
        std::printf("INVAL stored=%u dropped_ib=%u again=%u vb_after=%u left=%u overflow=%d dropped_many=%u live=%u\n", stored, dropped_ib, again, vb_after,
                    left, overflowed, dropped_many, c.index.live);
        // Index churn: random stores (overwriting ways and their index slots) and invalidations against a brute-force
        // count of the entries per identity; the index must never miss an entry (backward-shift deletion included).
        c.clear();
        unsigned seed = 12345, mismatches = 0, checks = 0;
        auto next_random = [&seed]() { seed = seed * 1103515245u + 12345u; return seed >> 8; };
        for (unsigned step = 0; step < 100000; ++step) {
            if ((step & 1023) == 0) c.begin_frame();
            const std::uintptr_t id = 0x10000 + 4 * (next_random() % 1500);
            if (next_random() % 5) { Key r; r.stride = 12; r.position_type = 2; r.primitives = 1 + next_random() % 2; r.vb = next_random() % 2000;
                                     c.store(r, &hs, id, (next_random() & 1) ? id + 0x100000 : 0); }
            else {
                unsigned expected = 0;
                for (unsigned i = 0; i < Cache::size; ++i) expected += c.entries[i].state != State::Empty && (c.entries[i].vb_identity == id || c.entries[i].ib_identity == id);
                mismatches += c.invalidate(id) != expected; ++checks;
                for (unsigned i = 0; i < Cache::size; ++i) mismatches += c.entries[i].state != State::Empty && (c.entries[i].vb_identity == id || c.entries[i].ib_identity == id);
            }
        }
        std::printf("CHURN checks=%u mismatches=%u live=%u full=%d\n", checks, mismatches, c.index.live, c.index.full);
        // Volatility: volatile_after writes make a wrapper volatile; a release forgets it; clear() forgets all.
        Volatility v{}; unsigned counts[5];
        for (unsigned i = 0; i < 5; ++i) counts[i] = v.bump(0x5000);
        const bool vol = v.is_volatile(0x5000); v.forget(0x5000); const bool forgotten = !v.is_volatile(0x5000);
        for (unsigned i = 0; i < volatile_after; ++i) v.bump(0x6000);
        const bool vol2 = v.is_volatile(0x6000); v.clear();
        std::printf("VOLATILE after=%u c3=%u c4=%u volatile=%d forgotten=%d volatile2=%d cleared=%d\n", volatile_after, counts[2], counts[3], vol, forgotten, vol2,
                    !v.is_volatile(0x6000));
    }
    return 0;
}
'''


def fields(line):
    return dict(part.split('=', 1) for part in line.split()[1:] if '=' in part)


class Harness:
    output = None

    @classmethod
    def lines(cls):
        if cls.output is None:
            compiler = shutil.which('clang++') or shutil.which('c++')
            if not compiler:
                raise unittest.SkipTest('no C++ compiler on the host')
            with tempfile.TemporaryDirectory(prefix='thin-vote-') as directory:
                source, exe = Path(directory) / 'harness.cpp', Path(directory) / 'harness'
                source.write_text(HARNESS)
                build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'), str(source), '-o', str(exe)],
                                       capture_output=True, text=True)
                if build.returncode:
                    raise AssertionError(build.stderr)
                run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
                if run.returncode:
                    raise AssertionError(run.stderr)
                cls.output = run.stdout
        return cls.output.splitlines()

    @classmethod
    def rows(cls, tag):
        return [fields(line) for line in cls.lines() if line.startswith(tag + ' ')]


class ThinVoteArithmetic(unittest.TestCase):
    def test_histograms(self):
        self.assertEqual(Harness.rows('MEASURE')[0], {'struts': '1', 'panel': '1', 'mixed': '1'})
        hist = {name: h for name, h in ((line.split()[1], fields(line)) for line in Harness.lines() if line.startswith('HIST '))}
        # Struts: h = 0.01 units, floor(log2) = -7, the tallest, so e0 = -14 and all six triangles in bin 7.
        self.assertEqual((hist['struts']['e0'], hist['struts']['total'], hist['struts']['c7'], hist['struts']['c8']), ('-14', '6', '0.000000', '1.000000'))
        # Panel: h = 1 / sqrt(2), floor(log2) = -1, e0 = -8.
        self.assertEqual((hist['panel']['e0'], hist['panel']['total'], hist['panel']['c7']), ('-8', '2', '0.000000'))
        # Mixed: the panel anchors e0 = -8; the six strut triangles sit in bin 1 (floor(log2 0.01) = -7), the panel in bin 7.
        self.assertEqual((hist['mixed']['e0'], hist['mixed']['total'], hist['mixed']['c1'], hist['mixed']['c2'], hist['mixed']['c7']), ('-8', '8', '0.000000', '0.750000', '0.750000'))

    def test_window_and_distance(self):
        flags = {float(r['px_per_unit']): r for r in Harness.rows('FLAG')}
        # 150 px/unit: struts 1.5 px (in [0.5, 3]), panel 106 px: struts 1, panel 0, mixed 0.75 -> alpha 0 and 0.25.
        self.assertAlmostEqual(float(flags[150]['log2']), 7.2288, places=2)
        self.assertAlmostEqual(float(flags[150]['struts']), 1.0, places=5)
        self.assertEqual(float(flags[150]['panel']), 0.0)
        self.assertAlmostEqual(float(flags[150]['mixed']), 0.75, places=5)
        self.assertAlmostEqual(float(flags[150]['alpha_struts']), 0.0, places=5)
        self.assertAlmostEqual(float(flags[150]['alpha_mixed']), 0.25, places=5)
        # 600 px/unit: struts 6 px, above the window: nothing votes. 30 px/unit: struts 0.3 px, below it.
        for px in (600, 30):
            self.assertEqual(float(flags[px]['struts']), 0.0, px)
            self.assertEqual(float(flags[px]['alpha_struts']), 1.0, px)
        # 60 px/unit: struts 0.6 px, just inside the lower edge; 290 px/unit: 2.9 px, just inside the upper edge (the bin
        # holds [2^-7, 2^-6) units, linear in log2 inside it, so an edge cuts the bin's share).
        self.assertGreater(float(flags[60]['struts']), 0.0)
        self.assertGreater(float(flags[290]['struts']), 0.0)
        for row in flags.values():
            self.assertEqual(float(row['panel']), 0.0)

    def test_bin_shift(self):
        shift = Harness.rows('SHIFT')[0]
        self.assertAlmostEqual(float(shift['a']), float(shift['b']), places=4)

    def test_rows_scale(self):
        rows = Harness.rows('ROWS')[0]
        self.assertEqual(rows['ok'], '1')
        self.assertAlmostEqual(float(rows['log2']), 6.0, places=2)
        self.assertEqual(rows['behind'], '0')

    def test_vote_threshold(self):
        vote = Harness.rows('VOTE')[0]
        self.assertEqual(float(vote['f40']), 1.0)
        self.assertAlmostEqual(float(vote['f50']), 0.5)
        self.assertAlmostEqual(float(vote['f75']), 0.25)
        self.assertEqual(float(vote['f100']), 0.0)

    def test_half_index32_bias_and_refusals(self):
        half = Harness.rows('HALF')[0]
        # A 0.25 x 8 quad in FLOAT16_4 behind INDEX32 indices biased by 10: two triangles, h ~ 0.2496, both in the top bin.
        self.assertEqual((half['ok'], half['e0'], half['total'], half['top']), ('1', '-10', '2', '1.000000'))
        self.assertEqual(half['out_of_window'], '0')
        self.assertEqual((half['degenerate_ok'], half['degenerate_total']), ('1', '1'))

    def test_cache(self):
        cache = Harness.rows('CACHE')[0]
        self.assertEqual(cache, {'miss': '1', 'known': '1', 'unreadable': '1', 'attempts': '8', 'null_store': '1'})
        self.assertEqual(Harness.rows('EVICT')[0], {'stored': '4', 'refused': '1'})
        self.assertEqual(Harness.rows('EVICT_NEXT')[0], {'stored': '1'})

    def test_invalidation_index(self):
        row = Harness.rows('INVAL')[0]
        self.assertEqual(row, {'stored': '18', 'dropped_ib': '4', 'again': '0', 'vb_after': '0', 'left': '4', 'overflow': '1',
                               'dropped_many': '10', 'live': '1'})
        churn = Harness.rows('CHURN')[0]
        self.assertGreater(int(churn['checks']), 15000)
        self.assertEqual(churn['mismatches'], '0')
        self.assertEqual(churn['full'], '0')
        self.assertEqual(Harness.rows('VOLATILE')[0], {'after': '4', 'c3': '3', 'c4': '4', 'volatile': '1', 'forgotten': '1', 'volatile2': '1', 'cleared': '1'})


def load_manage():
    spec = importlib.util.spec_from_file_location('thin_vote_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ThinVoteLaunch(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'fixture')  # a modded dry run checks the installed DLL against its manifest
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'fixture').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, inherited=None):
        code, output, error = self.launch(directory, *args, inherited=inherited)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_default_off_not_forwarded_and_inherited_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_THIN_VOTE', self.env(directory, *TAA, *LANE))
            self.assertNotIn('X3M_TAA_THIN_VOTE', self.env(directory, *TAA, *LANE, inherited={'X3M_TAA_THIN_VOTE': 'on'}))

    def test_on_and_off_are_forwarded(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory, *TAA, *LANE, '--taa-thin-vote', 'on')['X3M_TAA_THIN_VOTE'], 'on')
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-vote', 'off')['X3M_TAA_THIN_VOTE'], 'off')

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('1', 'yes', 'strut'):
                code, _, error = self.launch(directory, *TAA, *LANE, '--taa-thin-vote', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-thin-vote', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-thin-vote', 'off')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-thin-vote requires --taa', error)
            code, _, error = self.launch(directory, *TAA, '--taa-thin-vote', 'on')  # no lane
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-thin-vote on requires --motion-output --ownership --sun-shadow-lane', error)
            code, _, error = self.launch(directory, '--vanilla', '--taa-thin-vote', 'off')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-thin-vote cannot be combined with --vanilla', error)


class ThinVoteSource(unittest.TestCase):
    def test_dll_parses_off_by_default_and_uploads_c218_only_with_the_option(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('bool taa_thin_vote = false;', capture)
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_THIN_VOTE",setting,32)', capture)
        self.assertIn('if(enabled)renderer::material_motion_configure_thin_vote(true);', capture)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('renderer::MaterialMotionAbi::pixel_coordinates_constant, thin_vote_upload_ ? pixel_thin : pixel, thin_vote_upload_ ? 3u : 2u);', motion)
        self.assertIn('SetPixelShaderConstantF, 216, shadow_.ps_reserved, thin_vote_upload_ ? 3u : 2u));', motion)
        # c216 and c217 keep their eight values (the off path uploads that array alone); c218.x is the thin alpha.
        self.assertIn('previous_rows ? 1.f : 0.f, lightmap_widen_draw_scale_[0], lightmap_widen_draw_scale_[1], lightmap_fade_gain_};', motion)
        self.assertIn('if (thin_vote_upload_) { std::memcpy(pixel_thin, pixel, sizeof pixel); pixel_thin[9] = pixel_thin[10] = pixel_thin[11] = 0.f; thin_vote_alpha(route, rows.data(), pixel_thin[8]); }', motion)
        # A write or release of a watched buffer invalidates its entries (no per-draw revision lookup); WRITEONLY storage is refused.
        self.assertIn('if (ownership::buffer_invalidations_pending()) drain_thin_invalidations();', motion)
        self.assertIn('ownership::watch_buffer_writes(vb);', motion)
        self.assertNotIn('get_buffer_lock_view_light(reinterpret_cast<IDirect3DResource9*>(shadow_.stream0_identity)', motion)
        self.assertIn('outcome = Outcome::Unreadable; ++t.not_readable;', motion)
        loader = (ROOT / 'src/proxy/loader.cpp').read_text()
        self.assertIn('options.readable_managed_buffers = readable_buffers_enabled;', loader)
        # The readable policy is armed on the same gate hook_device enables the vote on (never on the raw variable).
        self.assertIn('readable_buffers_enabled = ownership_enabled && x3m::thin_vote_route_gate();', loader)
        self.assertNotIn('L"X3M_TAA_THIN_VOTE"', loader)
        self.assertIn('thin_vote_gate=taa_thin_vote&&motion_output_requested&&taa_requested&&hdr_requested&&lane&&wrapped;', capture)
        self.assertIn('const bool enabled=thin_vote_gate&&sun_lane_enabled;', capture)
        # A successful Lock is unlocked even without a pointer; a geometry refusal is watched.
        self.assertIn('if (iheld) ib->Unlock();', motion)
        self.assertIn('if (vheld) vb->Unlock();', motion)
        self.assertIn('outcome = Outcome::Unreadable; ++t.geometry; watch = true;', motion)
        ownership = (ROOT / 'src/ownership/d3d9_ownership.cpp').read_text()
        self.assertIn('plan=portable_upload::plan_creation(finite_plan||device->options.readable_managed_buffers,length,usage,pool,shared);', ownership)
        header = (ROOT / 'src/renderer/material_motion.h').read_text()
        self.assertIn('static constexpr unsigned pixel_thin_constant = 218;', header)

    def test_plain_programs_keep_their_bytecode(self):
        # bytecode_sha256 prefixes at fd60e44b (before the vote): the option-off tests draws, the current-depth and motion
        # fragments; the header each record names is the checked-in one.
        expected = {'temporal-line-mask': '802ff929a19f234b', 'temporal-line-mask-camera': '332fa59537d91f63',
                    'temporal-line-mask-depth': 'ee283dc7dd0eae45', 'temporal-line-mask-camera-depth': '5bacb6fef06e0290',
                    'current-depth-pixel': '33185f650fa2b17c', 'rigid-motion-pixel': 'a604ce8c772ac472'}
        for name, prefix in expected.items():
            record = json.loads((ROOT / f'verification/results/{name}-program.json').read_text())
            self.assertTrue(record['bytecode_sha256'].startswith(prefix), name)
            header = (ROOT / 'src/renderer' / (name.replace('-', '_') + '_program_inc.h')).read_bytes()
            self.assertEqual(hashlib.sha256(header).hexdigest(), record['header_sha256'], name)

    def test_thin_programs_recorded(self):
        for name in ('current-depth-thin-pixel', 'temporal-line-mask-depth-thin', 'temporal-line-mask-camera-depth-thin'):
            record = json.loads((ROOT / f'verification/results/{name}-program.json').read_text())
            self.assertEqual(record['target'], 'ps_3_0', name)


if __name__ == '__main__':
    unittest.main()
