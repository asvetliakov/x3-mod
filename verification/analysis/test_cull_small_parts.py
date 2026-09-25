"""Host checks of the small-parts cull trampoline (src/proxy/cull_small_parts_core.h).

The site verifier on a synthetic image (the 56-byte window, whole
instructions, the flag writer inside the displaced span, the cull target,
interior-branch, extra-source and changed-byte refusal, claim disjointness),
the source constants, the stub encoder, the threshold rule against the
tracked run131 rows, the install/value/frame line parsers, the core compiled
with the host compiler, the census classification with a threshold and the
projectile exemption, and the --cull-small-parts launcher gate with its
scope and projectile switches (--dry-run only, never a launch). The
installed executable is only read when present. No Wine, no game.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import verify_cull_small_parts_site as probe  # noqa: E402
import verify_cull_census_sites as census_probe  # noqa: E402
from verification.analysis.test_chase_aim_sites import synthetic_image  # noqa: E402

HARNESS = r'''
#include "cull_small_parts_core.h"
#include "cull_census_core.h"
#include <cstdio>
#include <cstring>
using namespace x3m::cull_small_parts::core;
int main() {
    unsigned failures = 0;
    auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } };
    float m00; std::uint32_t bits = 0x3f4ccccc; std::memcpy(&m00, &bits, 4);
    check(threshold_for(2.0, m00, 1280) == 3 && threshold_for(4.0, m00, 1280) == 6 && threshold_for(8.0, m00, 1280) == 11, "run131 thresholds 3/6/11");
    check(threshold_for(2.0, 0.8f, 1280) == 3 && threshold_for(2.0, 0.8f, 1920) == 2 && threshold_for(0.5, 1.0f, 1280) == 1, "other scales");
    check(threshold_for(0.0, m00, 1280) == 0 && threshold_for(-1.0, m00, 1280) == 0 && threshold_for(65.0, m00, 1280) == 0 && threshold_for(2.0, 0.0f, 1280) == 0 && threshold_for(2.0, m00, 32) == 0, "unusable inputs give 0");
    check(threshold_for(64.0, 0.06f, 64) == 21334 && threshold_for(64.0, 0.06f, 16384) == 84 && threshold_max == 0x1000000, "band extremes (the cap is beyond the band)");
    check(threshold_for(2.0, m00, 1280, 0x4000) == 3 && threshold_for(8.0, m00, 1280, focus_default) == 11 && focus_default == 0x4000, "F 0x4000 is the default");
    check(threshold_for(2.0, m00, 1280, 0x3470) == 4 && threshold_for(4.0, m00, 1280, 0x3470) == 7 && threshold_for(8.0, m00, 1280, 0x3470) == 13, "F 0x3470 (--fov default): 4/7/13");
    check(threshold_for(2.0, m00, 1280, 0x105) == 0 && threshold_for(2.0, m00, 1280, 0x8001) == 0 && threshold_for(2.0, m00, 1280, 0x106) > 0 && threshold_for(2.0, m00, 1280, 0x8000) == 2, "focus band 0x106..0x8000");
    check(focus_from_projection(m00, 1.33333337f) == 0x4000 && focus_from_projection(0.375f, 1.33333337f) == 0x4000 && focus_from_projection(0.5f, 1.7777636f) == 0x3470 &&
          focus_from_projection(m00, 3.9999745f) == 0x1a38 && focus_from_projection(1.0f, 1.25f) == 0x4000, "focus from P[0]/P[5]: 0x4000, 5120x1440 0x3470, zoom x2 0x1a38, 5:4");
    check(focus_from_projection(0.0f, 1.3f) == 0 && focus_from_projection(0.8f, 0.0f) == 0 && focus_from_projection(0.8f, -1.0f) == 0 && focus_from_projection(0.8f, 500.0f) == 0,
          "focus from the projection: unusable terms or F below 0x106 give 0");
    {
        // run309's vanilla projection is 16383.0 unrounded (the engine's own ~1e-4 error): the default within focus_snap, factor exactly 1.
        const float m11_3ffc = static_cast<float>(1.0 / std::tan(0x3ffc * 3.14159265358979323846 / 65536.0) / 0.75);
        const float m11_4003 = static_cast<float>(1.0 / std::tan(0x4003 * 3.14159265358979323846 / 65536.0) / 0.75);
        check(focus_from_projection(0.3750374f, 1.333461f) == 0x4000 && focus_from_projection(0.3750014f, 1.333333f) == 0x4000 && focus_snap == 2.0,
              "run309 vanilla projection (m00 0.3750374, m11 1.333461) and the menu's 0x4000 both give 0x4000");
        check(focus_from_projection(0.375f, m11_3ffc) == 0x3ffc && focus_from_projection(0.375f, m11_4003) == 0x4003, "outside focus_snap the nearest F stays (0x3ffc, 0x4003)");
        SceneLatch latch; latch.note(0.3750374f, 1.333461f);
        check(latch.focus == 0x4000 && threshold_for(4.0, 0.3750374f, 5120, latch.focus) == threshold_for(4.0, 0.3750374f, 5120), "a vanilla latch applies factor 1.0");
    }
    {
        // run309 (5120x1440, --fov 0x3470): the live buffer at Present (begin_frame) holds the HUD view (P[0] 0.375, F 0x4000);
        // the scene Clear latches the scene view (P[0] 0.5, F 0x3470) after it. The factor follows the scene.
        const float hud00 = 0.375f, hud11 = 1.33333337f, scene00 = 0.5f, scene11 = 1.7777636f;
        SceneLatch latch;
        Choice c = choose(latch, hud00, hud11, 0x3470);
        check(!latch.usable() && c.source == Source::registry && c.focus == 0x3470 && c.m00 > 0.49995f && c.m00 < 0.50005f && c.fallback == Fallback::no_scene,
              "no scene latch: the registry F, the HUD projection rescaled to it (0.5), fallback no_scene");
        latch.note(scene00, scene11);
        c = choose(latch, hud00, hud11, 0x4000);
        check(latch.usable() && c.source == Source::scene && c.m00 == scene00 && c.focus == 0x3470 && c.fallback == Fallback::none, "HUD live, scene latched: the scene's P[0] and F 0x3470");
        check(threshold_for(8.0, c.m00, 5120, c.focus) == 5 && threshold_for(8.0, hud00, 5120, 0x4000) == 6 && threshold_for(4.0, c.m00, 5120, c.focus) == 3,
              "8 px at 5120: 5 from the scene (6 from the HUD's 0x4000); 4 px: 3 either way");
        latch.note(0.8f, 0.0f);
        check(latch.usable() && latch.m00 == scene00 && latch.focus == 0x3470, "an unusable projection leaves the latch");
        for (unsigned i = 0; i < scene_max_age; ++i) latch.advance();
        check(latch.usable(), "the latch holds for scene_max_age frames");
        latch.advance(); c = choose(latch, hud00, hud11, 0x471c);
        check(!latch.usable() && c.source == Source::registry && c.focus == 0x471c && c.m00 < 0.3148f && c.m00 > 0.3146f, "aged out: the registry F (0x471c, P[0] 0.3147)");
        check(c.fallback == Fallback::aged, "aged out: fallback aged");
        latch.note(scene00, scene11); latch.clear();
        check(latch.fallback() == Fallback::reset && !std::strcmp(fallback_name(Fallback::reset), "reset") && !std::strcmp(fallback_name(Fallback::no_scene), "no_scene") &&
              !std::strcmp(fallback_name(Fallback::aged), "aged") && !std::strcmp(fallback_name(Fallback::none), "none"), "clear (Reset): fallback reset; names");
        check(!latch.usable() && fallback_m00(0.8f, 0.0f, 0x3470) == 0.8f && fallback_m00(0.8f, 1.0f, 0x105) == 0.8f, "clear (Reset); no P[5] or F outside the band: the live P[0]");
        check(!std::strcmp(source_name(Source::scene), "scene") && !std::strcmp(source_name(Source::registry), "registry"), "source names");
    }
    double px = 0;
    check(parse_px("2", &px) && px == 2.0 && parse_px("+2.5", &px) && px == 2.5 && parse_px(".5", &px) && px == 0.5 && !parse_px("2,5", &px) && !parse_px("1e1", &px) && !parse_px("", &px) && !parse_px(nullptr, &px), "parser");
    check(valid_px(64.0) && !valid_px(64.01) && !valid_px(0.0), "band");
    unsigned char s[stub_length]; encode_stub(0x10000000, 0x20000000, 0x20000004, 0x20000008, 0x0047d2c3, 0x10000054, s, Scope::all, true);
    std::uint32_t v = 0;
    check(stub_length == 82 && stub_projectile == 22 && stub_replay == 34 && stub_cull == 59 && stub_exempt == 70 && stub_continue == 76 && stub_scope_branch == 39, "stub layout");
    std::memcpy(&v, s + 2, 4); check(s[0] == 0x83 && s[1] == 0x3d && v == 0x20000000 && s[6] == 0 && s[7] == 0x7e && s[8] == stub_continue - 9, "cmp dword [threshold],0; jle continue");
    std::memcpy(&v, s + 11, 4); check(s[9] == 0x50 && s[10] == 0xa1 && v == 0x20000000 && !std::memcmp(s + 15, "\x39\x44\x24\x30\x58\x7d", 6) && s[21] == stub_continue - 22, "push eax; mov eax,[threshold]; cmp [esp+0x30],eax; pop eax; jge continue");
    check(!std::memcmp(s + 22, "\xf7\x87\x30\x01\x00\x00\x00\x00\x00\x20\x75", 11) && s[33] == stub_exempt - 34 && flags130_offset == 0x130 && projectile_flag == 0x20000000u, "test dword [edi+0x130],0x20000000; jne exempt");
    check(!std::memcmp(s + 34, "\x8b\x4f\x18\x85\xc9\x8b\x87\xd8\x01\x00\x00\x74", 12) && s[46] == stub_cull - 47 && !std::memcmp(s + 47, "\x8b\x89\xd8\x01\x00\x00\x3b\xc8\x7e", 9) && s[56] == stub_cull - 57 && s[57] == 0x8b && s[58] == 0xc1, "the engine's limit computation replayed");
    std::memcpy(&v, s + 61, 4); check(s[59] == 0xff && s[60] == 0x05 && v == 0x20000004, "inc dword [culled]");
    std::memcpy(&v, s + 66, 4); check(s[65] == 0xe9 && 0x10000046 + v == 0x0047d2c3, "jmp cull target");
    std::memcpy(&v, s + 72, 4); check(s[70] == 0xff && s[71] == 0x05 && v == 0x20000008, "inc dword [exempt]");
    std::memcpy(&v, s + 78, 4); check(s[76] == 0xff && s[77] == 0x25 && v == 0x10000054, "jmp [next]");
    unsigned char o[stub_length]; encode_stub(0x10000000, 0x20000000, 0x20000004, 0x20000008, 0x0047d2c3, 0x10000054, o, Scope::all, false);
    check(!std::memcmp(o, s, stub_projectile) && !std::memcmp(o + stub_replay, s + stub_replay, stub_length - stub_replay) && o[22] == 0xeb && 24 + o[23] == stub_replay && o[24] == 0xcc && o[33] == 0xcc, "projectiles off: jmp over the marker test, int3 padding, nothing else differs");
    unsigned char b[stub_length]; encode_stub(0x10000000, 0x20000000, 0x20000004, 0x20000008, 0x0047d2c3, 0x10000054, b, Scope::bodies, true);
    check(!std::memcmp(b, s, stub_scope_branch) && !std::memcmp(b + stub_cull, s + stub_cull, stub_length - stub_cull), "bodies stub: only bytes 39..58 differ");
    check(!std::memcmp(b + 34, site, site_length) && b[39] == 0x75 && 41 + b[40] == stub_continue && !std::memcmp(b + 41, "\x8b\x87\xd8\x01\x00\x00\xeb", 7) && 49 + b[48] == stub_cull && b[49] == 0xcc && b[58] == 0xcc, "bodies stub: displaced test; jne continue; mov eax,[edi+0x1d8]; jmp cull");
    bool ex = false;
    check(parse_projectiles(nullptr, &ex) && ex && parse_projectiles("", &ex) && ex && parse_projectiles("off", &ex) && !ex && parse_projectiles("on", &ex) && ex && !parse_projectiles("On", &ex) && !parse_projectiles("0", &ex), "projectiles parser: on (default), off, nothing else");
    check(!std::memcmp(marker_store, "\xc7\x44\x24\x20\x00\x00\x80\x20", marker_store_length) && !std::memcmp(marker_or + 7, "\x09\x90\x30\x01\x00\x00", 6) && marker_or_length == 13, "marker instructions");
    Scope sc = Scope::bodies;
    check(parse_scope(nullptr, &sc) && sc == Scope::all && parse_scope("bodies", &sc) && sc == Scope::bodies && !parse_scope("All", &sc) && !parse_scope("parts", &sc) && parse_scope("all", &sc) && sc == Scope::all, "scope parser");
    check(!std::strcmp(scope_name(Scope::bodies), "bodies") && !std::strcmp(scope_name(Scope::all), "all"), "scope names");
    check(std::memcmp(window + site_offset, site, site_length) == 0 && window[cull_offset] == 0x83 && window[cull_offset + 1] == 0xa7 && window[window_length - 2] == 0xeb && window[window_length - 1] == 0x05, "site and cull bytes inside the window");
    check(window_va + site_offset == site_va && site_va + site_length == next_va && window_va + cull_offset == cull_va && window_va + window_length + 5 == after_cull_va, "address relations");
    using namespace x3m::cull_census::core;
    Entry e{}; e.exited = 1; e.flags_in = 0x1002; e.flags_out = 0x1000; e.s = 2; e.measure = 4; e.limit = 0;
    check(classify(e) == Verdict::culled_other && classify(e, 3) == Verdict::culled_small && classify(e, 2) == Verdict::culled_other, "census: culled_small below the threshold only");
    e.limit = 8; check(classify(e, 3) == Verdict::culled_size, "census: the engine's size cull named first");
    e.limit = 0; e.measure = 0; e.s = 1; check(classify(e, 3) == Verdict::culled_min, "census: the engine's degenerate cull named first");
    e.flags_in = 0x4001002; check(classify(e, 3) == Verdict::culled_small, "census: a 0x4000000 node below the threshold is the stub's");
    check(classify(e, 3, true) == Verdict::culled_small, "census, scope bodies: a parentless node below the threshold is the stub's");
    e.parent = 0x1000; check(classify(e, 3, true) == Verdict::culled_other && classify(e, 3, false) == Verdict::culled_small, "census, scope bodies: a parented node is never the stub's");
    e.parent = 0;
    e.flags130 = x3m::cull_census::core::projectile_flag;
    check(classify(e, 3, false, true) == Verdict::culled_other && classify(e, 3, false, false) == Verdict::culled_small && classify(e, 3, true, true) == Verdict::culled_other, "census: an exempt projectile below the threshold is never the stub's");
    check(small_exempt(e, 3, true) && !small_exempt(e, 3, false) && !small_exempt(e, 1, true) && !small_exempt(e, 0, true), "census: the exempt count needs the exemption on and s below the threshold");
    e.flags130 = 0x00800000; check(classify(e, 3, false, true) == Verdict::culled_small && !small_exempt(e, 3, true), "census: bit 0x800000 alone is not the marker");
    check(x3m::cull_census::core::projectile_flag == x3m::cull_small_parts::core::projectile_flag && x3m::cull_census::core::flags130_offset == x3m::cull_small_parts::core::flags130_offset, "census and stub share the marker");
    e.flags130 = 0;
    e.flags_out = 0x1002; check(classify(e, 3) == Verdict::kept, "census: kept stays kept");
    check(!std::strcmp(verdict_name(Verdict::culled_small), "culled_small") && verdict_count == 6, "verdict name");
    std::printf("cull_small_parts_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


def load_manage():
    spec = importlib.util.spec_from_file_location('cull_small_parts_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def image(*changes):
    """A synthetic PE with the window, its documented incoming branches, the after-cull target and the final ret 8."""
    extra = [(probe.FUNCTION[0], probe.PROLOGUE), *probe.S_STORES.items(), (probe.WINDOW_VA, probe.WINDOW), (0x47d28c, b'\x74\x14'),                     # je 0x47d2a2, the engine's other incoming branch
             (probe.AFTER_CULL_VA, b'\x8b\x87\x2c\x01\x00\x00'), (probe.RET_VA, b'\xc2\x08\x00'),
             (probe.MARKER_STORE_VA, probe.MARKER_STORE), (probe.MARKER_OR_VA, probe.MARKER_OR), (probe.MARKER_USE_VA, probe.MARKER_USE), *changes]
    return synthetic_image(extra=extra, text_size=0x100000)


def inspect_image(data):
    return probe.inspect(data, probe.decode(data), probe.CORE.read_text())


class CullSmallPartsSite(unittest.TestCase):
    def test_synthetic_image_passes_all_but_identity(self):
        report = inspect_image(image())
        checks = {k: v for k, v in report['checks'].items() if k != 'exe_identity'}
        self.assertTrue(all(checks.values()), report)
        self.assertFalse(report['checks']['exe_identity'])
        self.assertEqual(report['site_sources'], ['0x47d28c', '0x47d297'])
        self.assertEqual(report['interior_branches'], [])
        self.assertEqual(len(report['checks']), 21)

    def test_changed_bytes_and_branches_refused(self):
        cases = {
            'window_bytes': (probe.WINDOW_VA + 2, b'\x15'),                                  # cmp esi,0x15
            'site_whole_instructions': (probe.SITE_VA + 2, b'\x1c'),                         # mov ecx,[edi+0x1c]
            'next_instruction': (probe.NEXT_VA + 2, b'\xdc'),                                # mov eax,[edi+0x1dc]
            'je_consumes_flags': (probe.JE_VA, b'\xeb\x0c'),                                 # jmp instead of je
            'cull_instruction': (probe.CULL_VA + 6, b'\xfb'),                                # and ..,0xfffffffb
            'no_interior_branch': (0x47d300, b'\xe9' + struct.pack('<i', probe.SITE_VA + 3 - (0x47d300 + 5))),
            'site_sources': (0x47d400, b'\xe9' + struct.pack('<i', probe.SITE_VA - (0x47d400 + 5))),
            'window_branches_contained': (probe.WINDOW_VA + 4, b'\x40'),                     # jge far outside the window
            'prologue_frame': (probe.FUNCTION[0] + 2, b'\x18'),                             # sub esp,0x18
            's_slot_stores': (0x47d24a + 3, b'\x28'),                                       # mov [esp+0x28],eax
            'function_ret': (probe.RET_VA, b'\xc2\x04\x00'),
            'projectile_marker': (probe.MARKER_STORE_VA + 7, b'\x00'),                      # the class-0 case stores 0x00800000
        }
        for failed, change in cases.items():
            with self.subTest(check=failed):
                report = inspect_image(image(change))
                self.assertFalse(report['checks'][failed], report)

    def test_claims_disjoint_and_constants(self):
        self.assertEqual(probe.source_constants(probe.CORE.read_text()), probe.EXPECTED_CONSTANTS)
        self.assertEqual(probe.SITE, bytes.fromhex('8b4f1885c9'))
        self.assertEqual(len(probe.WINDOW), 56)
        self.assertEqual(probe.WINDOW[probe.CULL_VA - probe.WINDOW_VA:probe.CULL_VA - probe.WINDOW_VA + 7], probe.CULL)
        claim = (probe.SITE_VA, probe.SITE_VA + 5)
        for name, (address, length) in probe.OTHER_CLAIMS.items():
            with self.subTest(claim=name):
                self.assertTrue(claim[1] <= address or address + length <= claim[0])
        self.assertEqual(probe.OTHER_CLAIMS['cull_census_measure'][0], census_probe.MEASURE_SITE_VA)
        self.assertEqual(probe.OTHER_CLAIMS['cull_census_exit'][0], census_probe.EXIT_SITE_VA)
        self.assertIn('culled_small', census_probe.VERDICTS)

    def test_encoder_and_threshold(self):
        stub = probe.encode_stub(0x10000000, 0x20000000, 0x20000004, 0x20000008, probe.CULL_VA, 0x10000054)
        self.assertEqual(len(stub), 82)
        self.assertEqual(stub[:2], b'\x83\x3d')
        self.assertEqual(stub[6:9], b'\x00\x7e\x43')
        self.assertEqual(stub[9:15], b'\x50\xa1' + struct.pack('<I', 0x20000000))
        self.assertEqual(stub[15:22], b'\x39\x44\x24\x30\x58\x7d\x36')
        self.assertEqual(stub[22:34], bytes.fromhex('f787 30010000 00000020 7524'.replace(' ', '')))
        self.assertEqual(stub[34:59], bytes.fromhex('8b4f18 85c9 8b87d8010000 740c 8b89d8010000 3bc8 7e02 8bc1'.replace(' ', '')))
        self.assertEqual(stub[59:65], b'\xff\x05' + struct.pack('<I', 0x20000004))
        self.assertEqual(struct.unpack('<i', stub[66:70])[0], probe.CULL_VA - (0x10000000 + 70))
        self.assertEqual(stub[70:76], b'\xff\x05' + struct.pack('<I', 0x20000008))
        self.assertEqual(stub[76:82], b'\xff\x25' + struct.pack('<I', 0x10000054))
        bodies = probe.encode_stub(0x10000000, 0x20000000, 0x20000004, 0x20000008, probe.CULL_VA, 0x10000054, scope='bodies')
        self.assertEqual((bodies[:39], bodies[59:]), (stub[:39], stub[59:]))
        self.assertEqual(bodies[39:59], bytes.fromhex('7523 8b87d8010000 eb0a'.replace(' ', '')) + b'\xcc' * 10)
        off = probe.encode_stub(0x10000000, 0x20000000, 0x20000004, 0x20000008, probe.CULL_VA, 0x10000054, projectiles=False)
        self.assertEqual((off[:22], off[34:]), (stub[:22], stub[34:]))
        self.assertEqual(off[22:34], b'\xeb\x0a' + b'\xcc' * 10)
        self.assertTrue(probe.scope_stub_ok())
        self.assertTrue(probe.projectile_stub_ok())
        with self.assertRaises(ValueError):
            probe.encode_stub(1 << 32, 0, 0, 0, 0, 0)
        with self.assertRaises(ValueError):
            probe.encode_stub(0, 0, 0, 0, 0, 0, scope='parts')
        m00 = struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0]
        self.assertEqual((probe.threshold_for(2, m00, 1280), probe.threshold_for(4, m00, 1280), probe.threshold_for(8, m00, 1280)), (3, 6, 11))
        self.assertEqual(probe.threshold_for(2, 0.8, 1920), 2)
        self.assertEqual(probe.threshold_for(0, m00, 1280), 0)
        self.assertEqual(tuple(probe.threshold_for(px, m00, 1280, 0x3470) for px in (2, 4, 8)), (4, 7, 13))
        self.assertEqual(tuple(probe.threshold_for(px, m00, 1280, 0x4000) for px in (2, 4, 8)), (3, 6, 11))
        self.assertEqual((probe.threshold_for(2, m00, 1280, 0x105), probe.threshold_for(2, m00, 1280, 0x8001)), (0, 0))
        self.assertEqual((probe.focus_from_projection(0.5, 1.7777636), probe.focus_from_projection(m00, 3.9999745), probe.focus_from_projection(0.8, 0)), (0x3470, 0x1a38, 0))

    def test_tracked_rows_reproduce_the_census_classes(self):
        document = json.loads((ROOT / 'verification/fixtures/run131-cull-census-rows.json').read_text())
        rows = document['rows']
        self.assertEqual(len(rows), 1214)
        m00 = struct.unpack('<f', struct.pack('<I', int(document['projection_m00_bits'], 16)))[0]
        for px, expected in document['expected'].items():
            with self.subTest(px=px):
                threshold = probe.threshold_for(float(px), m00, document['width'])
                self.assertEqual(threshold, expected['threshold_s'])
                flipped = [r for r in rows if r[8] == 0 and r[0] < threshold]
                self.assertEqual((len(flipped), sum(r[9] for r in flipped)), (expected['nodes'], expected['draws']))
        # The rows carry no parent link: `bodies` is pinned as the fixture assigns parents (proven by limit > thr_1d8, else no body flag).
        mask = int(document['body_flags_mask'], 16)
        self.assertEqual(mask, 0x09000000)
        for px, expected in document['expected'].items():
            with self.subTest(px=px, scope='bodies'):
                flipped = [r for r in rows if r[8] == 0 and r[0] < expected['threshold_s'] and r[7] & mask and not r[6] > r[5]]
                self.assertEqual((len(flipped), sum(r[9] for r in flipped)), (expected['bodies_nodes'], expected['bodies_draws']))
        self.assertEqual([(document['expected'][px]['bodies_nodes'], document['expected'][px]['bodies_draws']) for px in ('2', '4', '8')], [(89, 395), (120, 450), (131, 471)])
        self.assertEqual(sum(1 for r in rows if r[6] > r[5] and r[8] == 0 and r[0] < 11), 0)
        self.assertEqual((document['expected']['2']['draws'], document['expected']['4']['draws'], document['expected']['8']['draws']), (403, 458, 479))
        self.assertTrue(all(r[7] & 2 for r in rows))

    def test_line_parsers(self):
        row = probe.parse_log_line('00:00:01.234 cull_small_parts requested=2 px=2 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active')
        self.assertEqual(row, {'requested': '2', 'px': 2.0, 'patched': True, 'reason': 'ok', 'site': 0x47d2a2, 'cull': 0x47d2c3, 'write': 'atomic', 'stub': 0x0a100000, 'camera': 'active', 'scope': None,
                               'projectiles': None})
        self.assertEqual(probe.parse_log_line('cull_small_parts requested=4 px=4 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active scope=all projectiles=marker_mismatch')['projectiles'],
                         'marker_mismatch')
        self.assertEqual(probe.parse_log_line(' cull_small_parts requested=2 px=2 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active scope=bodies')['scope'], 'bodies')
        self.assertIsNone(probe.parse_log_line('cull_small_parts requested=2 px=0 patched=0 reason=bytes_mismatch'))
        self.assertEqual(probe.parse_value_line('cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=3'), {'px': 2.0, 'm00': 0.799999952, 'width': 1280, 'threshold': 3, 'focus': None,
                                                                                                                      'source': None, 'fallback': None})
        self.assertEqual(probe.parse_value_line('cull_small_parts_value px=2 m00=0.5 width=5120 threshold=1 focus=0x3470')['focus'], 0x3470)
        value = probe.parse_value_line('cull_small_parts_value px=4 m00=0.5 width=5120 threshold=3 focus=0x3470 source=scene fallback=none')
        self.assertEqual((value['focus'], value['source'], value['fallback']), (0x3470, 'scene', 'none'))
        frame = probe.parse_frame_line('cull_small_parts_frame device=1 frame=900 px=4 threshold=3 culled=12 m00=0.5 width=5120 scope=all projectiles=on exempt_bullet=0 focus=0x3470 '
                                       'source=registry fallback=no_scene')
        self.assertEqual((frame['focus'], frame['source'], frame['fallback'], frame['exempt_bullet']), (0x3470, 'registry', 'no_scene', 0))
        frame = probe.parse_frame_line('cull_small_parts_frame device=1 frame=4991 px=2 threshold=3 culled=1147 m00=0.799999952 width=1280')
        self.assertEqual((frame['frame'], frame['threshold'], frame['culled'], frame['width']), (4991, 3, 1147, 1280))
        self.assertIsNone(frame['scope'])
        self.assertEqual(probe.parse_frame_line('cull_small_parts_frame device=1 frame=4991 px=2 threshold=3 culled=806 m00=0.799999952 width=1280 scope=bodies')['scope'], 'bodies')
        frame = probe.parse_frame_line('cull_small_parts_frame device=1 frame=6137 px=4 threshold=4 culled=150 m00=0.799999952 width=1920 scope=all projectiles=on exempt_bullet=31')
        self.assertEqual((frame['projectiles'], frame['exempt_bullet']), ('on', 31))
        self.assertIsNone(probe.parse_frame_line('cull_small_parts_frame device=1 frame=4991 px=2 threshold=3 culled=806 m00=0.799999952 width=1280')['exempt_bullet'])
        self.assertIsNone(probe.parse_frame_line('cull_small_parts_value px=2 m00=0.8 width=1280 threshold=3'))

    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-cull-small-parts-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(HARNESS)
            executable = directory / 'cull_small_parts_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'cull_small_parts_core checks_failed=0\n')

    def test_scene_projection_hand_over(self):
        # The cull's FOV source is the motion route's scene-phase camera read (run309: the live buffer at Present is the HUD view).
        # The motion fixture cannot patch the cull site, so the hand-over is pinned in the source: dropping either call, or
        # reading the camera for the scene anywhere but the Background -> Scene Clear, fails here.
        source = (ROOT / 'src/proxy/motion_output.cpp').read_text()

        def body(signature):
            start = source.index(signature)
            depth, i = 0, source.index('{', start)
            for j in range(i, len(source)):
                depth += {'{': 1, '}': -1}.get(source[j], 0)
                if depth == 0:
                    return source[i:j + 1]
            self.fail(signature)

        read_camera = body('void MotionOutput::read_camera(bool scene) noexcept')
        call = 'cull_small_parts::note_scene_projection(sample.state.m00, sample.state.m11)'
        self.assertEqual(read_camera.count(call), 2, 'both camera paths (TAA/candidates and the cull alone) hand the scene projection over')
        no_taa, taa = read_camera.split('camera_state::Sample sample{};\n    const bool valid = camera_state::read(&sample);\n    ++counters_.camera_reads;')
        self.assertIn('const bool cull = scene && cull_small_parts::wants_scene_projection();', no_taa)
        self.assertIn('if (cull && valid) ' + call, no_taa)
        self.assertIn('if (scene) {', taa)
        self.assertLess(taa.index('if (scene) {'), taa.index(call))
        self.assertIn('if (valid) ' + call, taa)
        after_clear = body('void MotionOutput::after_clear(HRESULT result) noexcept')
        self.assertEqual(after_clear.count('read_camera(true)'), 1)
        self.assertIn('if (before == renderer::BoundaryState::Background && selector_.state() == renderer::BoundaryState::Scene) read_camera(true);', after_clear)
        self.assertEqual(source.count('read_camera(true)'), 1, 'the scene read happens only at the scene-phase Clear')
        self.assertIn('#include "cull_small_parts.h"', source)
        self.assertIn('cull_small_parts::begin_frame();', (ROOT / 'src/proxy/capture.cpp').read_text())

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)


class CullSmallPartsLaunchOption(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
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

    def test_absent_or_zero_drops_the_variable_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in ((), ('--cull-small-parts', '0')):
                code, output, _ = self.launch(directory, *args, inherited={'X3M_CULL_SMALL_PARTS_PX': '2', 'X3M_CULL_SMALL_PARTS_SCOPE': 'all',
                                                                          'X3M_CULL_SMALL_PARTS_PROJECTILES': 'off'})
                self.assertEqual(code, 0)
                self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', json.loads(output)['env'])
                self.assertNotIn('X3M_CULL_SMALL_PARTS_SCOPE', json.loads(output)['env'])
                self.assertNotIn('X3M_CULL_SMALL_PARTS_PROJECTILES', json.loads(output)['env'])

    def test_dry_run_carries_the_value(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--cull-small-parts', '2')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']},
                             {'X3M_CULL_SMALL_PARTS_PX': '2.0000', 'X3M_CULL_SMALL_PARTS_SCOPE': 'all', 'X3M_CULL_SMALL_PARTS_PROJECTILES': 'on'})

    def test_scope_forwarded_and_default_overrides_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, expected in ((('--cull-small-parts-scope', 'all'), 'all'), (('--cull-small-parts-scope', 'bodies'), 'bodies'), ((), 'all')):
                code, output, error = self.launch(directory, '--cull-small-parts', '2', *args, inherited={'X3M_CULL_SMALL_PARTS_SCOPE': 'all'})
                self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_CULL_SMALL_PARTS_SCOPE'], expected)

    def test_scope_refused_without_the_cull_or_with_an_unknown_value(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--cull-small-parts-scope', 'all'), ('--cull-small-parts', '0', '--cull-small-parts-scope', 'bodies')):
                code, _, error = self.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--cull-small-parts-scope requires a non-zero --cull-small-parts', error)
            code, _, error = self.launch(directory, '--cull-small-parts', '2', '--cull-small-parts-scope', 'parts')
            self.assertEqual(code, 2)
            self.assertIn('invalid choice', error)

    def test_projectiles_forwarded_default_on_and_refused_without_the_cull(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, expected in ((('--cull-small-parts-projectiles', 'off'), 'off'), (('--cull-small-parts-projectiles', 'on'), 'on'), ((), 'on')):
                code, output, error = self.launch(directory, '--cull-small-parts', '4', *args, inherited={'X3M_CULL_SMALL_PARTS_PROJECTILES': 'off'})
                self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_CULL_SMALL_PARTS_PROJECTILES'], expected)
            for args in (('--cull-small-parts-projectiles', 'on'), ('--cull-small-parts', '0', '--cull-small-parts-projectiles', 'off')):
                code, _, error = self.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--cull-small-parts-projectiles requires a non-zero --cull-small-parts', error)
            code, _, error = self.launch(directory, '--cull-small-parts', '4', '--cull-small-parts-projectiles', 'missiles')
            self.assertEqual(code, 2)
            self.assertIn('invalid choice', error)
            # Modded launch: on by default with the default cull, off on request, dropped with an explicit 0.
            code, output, error = self.modded_launch(directory)
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_CULL_SMALL_PARTS_PROJECTILES'], 'on')
            self.assertEqual(json.loads(self.modded_launch(directory, '--cull-small-parts-projectiles', 'off')[1])['env']['X3M_CULL_SMALL_PARTS_PROJECTILES'], 'off')
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '0', inherited={'X3M_CULL_SMALL_PARTS_PROJECTILES': 'on'})[1])['env']
            self.assertNotIn('X3M_CULL_SMALL_PARTS_PROJECTILES', env)

    def modded_launch(self, directory, *args, inherited=None):
        """A dry-run launch against a fake installed proxy, so the launcher
        default applies (no --vanilla)."""
        module = load_manage()
        game = Path(directory) / 'modded'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
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

    def test_modded_launch_defaults_to_four_px_scope_all(self):
        """Run 43 B default at 2 px, raised to 4 px on 2026-09-25 (stand-command promotion): every modded
        launch culls over all nodes; an explicit 0 is the off switch and --vanilla forwards nothing."""
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.modded_launch(directory)
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_CULL_SMALL_PARTS_PX'], env['X3M_CULL_SMALL_PARTS_SCOPE']), ('4.0000', 'all'))
            # An explicit value and an explicit scope still win.
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '2', '--cull-small-parts-scope', 'bodies')[1])['env']
            self.assertEqual((env['X3M_CULL_SMALL_PARTS_PX'], env['X3M_CULL_SMALL_PARTS_SCOPE']), ('2.0000', 'bodies'))
            # The scope alone is enough on a modded launch: the cull is on by default.
            code, output, error = self.modded_launch(directory, '--cull-small-parts-scope', 'bodies')
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_CULL_SMALL_PARTS_SCOPE'], 'bodies')
            # Explicit off, even with the variables inherited.
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '0',
                                                inherited={'X3M_CULL_SMALL_PARTS_PX': '8', 'X3M_CULL_SMALL_PARTS_SCOPE': 'bodies'})[1])['env']
            self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', env)
            self.assertNotIn('X3M_CULL_SMALL_PARTS_SCOPE', env)
            # --vanilla sets nothing and still refuses a bare scope.
            self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', json.loads(self.launch(directory)[1])['env'])

    def test_out_of_range_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('65', '-1', 'nan', '1e-7'):
                code, _, error = self.launch(directory, '--cull-small-parts', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--cull-small-parts out of range', error)


if __name__ == '__main__':
    unittest.main()
