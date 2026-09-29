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
from source_text import source_text

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
    unsigned char s[stub_length]; encode_stub(0x10000000, 0x20000000, 0x2000000c, 0x20000004, 0x20000008, 0x20000010, 0x0047d2c3, 0x10000094, s, true);
    std::uint32_t v = 0;
    check(stub_length == 147 && stub_continue == 63 && stub_pop_continue == 62 && stub_dock == 69 && stub_dock_projectile == 70 && stub_dock_count == 82 &&
          stub_small == 90 && stub_projectile == 91 && stub_count == 103 && stub_replay == 109 && stub_cull == 134 && stub_exempt == 139, "stub layout");
    std::memcpy(&v, s + 2, 4); check(s[0] == 0x83 && s[1] == 0x3d && v == 0x2000000c && s[6] == 0 && s[7] == 0x7e && 9 + s[8] == stub_continue, "cmp dword [upper],0; jle continue");
    std::memcpy(&v, s + 11, 4); check(s[9] == 0x50 && s[10] == 0xa1 && v == 0x2000000c && !std::memcmp(s + 15, "\x39\x44\x24\x30\x7d", 5) && 21 + s[20] == stub_pop_continue, "push eax; mov eax,[upper]; cmp [esp+0x30],eax; jge pop_continue");
    std::memcpy(&v, s + 22, 4); check(s[21] == 0xa1 && v == 0x20000000 && !std::memcmp(s + 26, "\x39\x44\x24\x30\x7c", 5) && 32 + s[31] == stub_small, "mov eax,[threshold]; cmp [esp+0x30],eax; jl pop_small");
    check(!std::memcmp(s + 32, "\x8b\x87\x40\x01\x00\x00\x2d\x20\xbf\xb8\x35\x3d\x40\x0d\x03\x00\x72", 17) && 50 + s[49] == stub_dock &&
          !std::memcmp(s + 50, "\x2d\x20\xb3\x81\x00\x3d\x40\x0d\x03\x00\x72", 11) && 62 + s[61] == stub_dock && model_offset == 0x140,
          "mov eax,[edi+0x140]; sub eax,901300000; cmp eax,200000; jb pop_dock; sub eax,8500000; cmp eax,200000; jb pop_dock");
    std::memcpy(&v, s + 65, 4); check(s[62] == 0x58 && s[63] == 0xff && s[64] == 0x25 && v == 0x10000094 && s[69] == 0x58 && s[90] == 0x58, "pop_continue: pop eax; continue: jmp [next]; pop_dock / pop_small: pop eax");
    check(!std::memcmp(s + 70, "\xf7\x87\x30\x01\x00\x00\x00\x00\x00\x20\x75", 11) && 82 + s[81] == stub_exempt && !std::memcmp(s + 91, s + 70, 11) && 103 + s[102] == stub_exempt &&
          flags130_offset == 0x130 && projectile_flag == 0x20000000u, "both paths: test dword [edi+0x130],0x20000000; jne exempt");
    std::memcpy(&v, s + 84, 4); check(s[82] == 0xff && s[83] == 0x05 && v == 0x20000010 && s[88] == 0xeb && 90 + s[89] == stub_replay, "inc dword [dock_culled]; jmp replay");
    std::memcpy(&v, s + 105, 4); check(s[103] == 0xff && s[104] == 0x05 && v == 0x20000004, "inc dword [culled]");
    check(!std::memcmp(s + 109, "\x8b\x4f\x18\x85\xc9\x8b\x87\xd8\x01\x00\x00\x74", 12) && 122 + s[121] == stub_cull && !std::memcmp(s + 122, "\x8b\x89\xd8\x01\x00\x00\x3b\xc8\x7e", 9) && 132 + s[131] == stub_cull && s[132] == 0x8b && s[133] == 0xc1, "the engine's limit computation replayed");
    std::memcpy(&v, s + 135, 4); check(s[134] == 0xe9 && 0x10000000u + stub_exempt + v == 0x0047d2c3, "jmp cull target");
    std::memcpy(&v, s + 141, 4); check(s[139] == 0xff && s[140] == 0x05 && v == 0x20000008 && s[145] == 0xeb && 147 + static_cast<signed char>(s[146]) == static_cast<int>(stub_continue), "inc dword [exempt]; jmp continue");
    unsigned char o[stub_length]; encode_stub(0x10000000, 0x20000000, 0x2000000c, 0x20000004, 0x20000008, 0x20000010, 0x0047d2c3, 0x10000094, o, false);
    check(!std::memcmp(o, s, stub_dock_projectile) && !std::memcmp(o + stub_dock_count, s + stub_dock_count, stub_projectile - stub_dock_count) &&
          !std::memcmp(o + stub_count, s + stub_count, stub_length - stub_count) && o[70] == 0xeb && 72 + o[71] == stub_dock_count && o[72] == 0xcc && o[81] == 0xcc &&
          o[91] == 0xeb && 93 + o[92] == stub_count && o[93] == 0xcc && o[102] == 0xcc, "projectiles off: jmp over both marker tests, int3 padding, nothing else differs");
    check(dock_model(901300000u) && dock_model(901300003u) && dock_model(901400003u) && dock_model(901499999u) && dock_model(909800000u) && dock_model(909900005u) && dock_model(909999999u) &&
          !dock_model(901299999u) && !dock_model(901500000u) && !dock_model(909799999u) && !dock_model(910000000u) && !dock_model(0u) && !dock_model(0xffffffffu) && !dock_model(0x10000u),
          "dock-port ids: [901300000, 901499999] and [909800000, 909999999] only");
    check(upper_for(3, 5) == 5 && upper_for(3, 0) == 3 && upper_for(3, 2) == 3 && upper_for(0, 5) == 0 && upper_for(-1, 5) == 0, "upper: the larger threshold, 0 when the small one is 0");
    bool ex = false;
    check(parse_projectiles(nullptr, &ex) && ex && parse_projectiles("", &ex) && ex && parse_projectiles("off", &ex) && !ex && parse_projectiles("on", &ex) && ex && !parse_projectiles("On", &ex) && !parse_projectiles("0", &ex), "projectiles parser: on (default), off, nothing else");
    check(!std::memcmp(marker_store, "\xc7\x44\x24\x20\x00\x00\x80\x20", marker_store_length) && !std::memcmp(marker_or + 7, "\x09\x90\x30\x01\x00\x00", 6) && marker_or_length == 13, "marker instructions");
    check(std::memcmp(window + site_offset, site, site_length) == 0 && window[cull_offset] == 0x83 && window[cull_offset + 1] == 0xa7 && window[window_length - 2] == 0xeb && window[window_length - 1] == 0x05, "site and cull bytes inside the window");
    check(window_va + site_offset == site_va && site_va + site_length == next_va && window_va + cull_offset == cull_va && window_va + window_length + 5 == after_cull_va, "address relations");
    using namespace x3m::cull_census::core;
    Entry e{}; e.exited = 1; e.flags_in = 0x1002; e.flags_out = 0x1000; e.s = 2; e.measure = 4; e.limit = 0;
    check(classify(e) == Verdict::culled_other && classify(e, 3) == Verdict::culled_small && classify(e, 2) == Verdict::culled_other, "census: culled_small below the threshold only");
    e.limit = 8; check(classify(e, 3) == Verdict::culled_size, "census: the engine's size cull named first");
    e.limit = 0; e.measure = 0; e.s = 1; check(classify(e, 3) == Verdict::culled_min, "census: the engine's degenerate cull named first");
    e.flags_in = 0x4001002; check(classify(e, 3) == Verdict::culled_small, "census: a 0x4000000 node below the threshold is the stub's");
    e.parent = 0x1000; check(classify(e, 3) == Verdict::culled_small, "census: a parented node below the threshold is the stub's too (scope all)");
    e.parent = 0;
    e.flags130 = x3m::cull_census::core::projectile_flag;
    check(classify(e, 3, true) == Verdict::culled_other && classify(e, 3, false) == Verdict::culled_small, "census: an exempt projectile below the threshold is never the stub's");
    check(small_exempt(e, 3, true) && !small_exempt(e, 3, false) && !small_exempt(e, 1, true) && !small_exempt(e, 0, true), "census: the exempt count needs the exemption on and s below the threshold");
    e.flags130 = 0x00800000; check(classify(e, 3, true) == Verdict::culled_small && !small_exempt(e, 3, true), "census: bit 0x800000 alone is not the marker");
    check(x3m::cull_census::core::projectile_flag == x3m::cull_small_parts::core::projectile_flag && x3m::cull_census::core::flags130_offset == x3m::cull_small_parts::core::flags130_offset, "census and stub share the marker");
    e.flags130 = 0;
    e.flags_out = 0x1002; check(classify(e, 3) == Verdict::kept, "census: kept stays kept");
    check(!std::strcmp(verdict_name(Verdict::culled_small), "culled_small") && verdict_count == 8, "verdict name");
    // X3M_CULL_DOCK_PARTS_PX: a dock-port row at or above the small threshold and below upper is culled_dock; the same row
    // below the small threshold stays culled_small; other ids, upper <= threshold and exempt projectiles are never culled_dock.
    {
        Entry d{}; d.exited = 1; d.flags_in = 0x1002; d.flags_out = 0x1000; d.s = 4; d.measure = 8; d.limit = 0; d.model = 901300003u;
        check(classify(d, 3, true, 5) == Verdict::culled_dock && classify(d, 3, true, 4) == Verdict::culled_other && classify(d, 3, true, 3) == Verdict::culled_other &&
              classify(d, 0, true, 5) == Verdict::culled_other, "census: culled_dock between the thresholds only");
        d.s = 2; check(classify(d, 3, true, 5) == Verdict::culled_small, "census: a dock-port row below the small threshold is culled_small");
        d.s = 4; d.model = 901500000u; check(classify(d, 3, true, 5) == Verdict::culled_other, "census: an id outside the ranges is never culled_dock");
        d.model = 909999999u; check(classify(d, 3, true, 5) == Verdict::culled_dock && !std::strcmp(verdict_name(Verdict::culled_dock), "culled_dock"), "census: the M6 range");
        d.flags130 = x3m::cull_census::core::projectile_flag; check(classify(d, 3, true, 5) == Verdict::culled_other && classify(d, 3, false, 5) == Verdict::culled_dock, "census: an exempt projectile is never culled_dock");
        d.flags130 = 0; d.flags_out = 0x1002; check(classify(d, 3, true, 5) == Verdict::kept && with_prop(Verdict::culled_dock, true) == Verdict::culled_dock, "census: kept stays kept");
        check(x3m::cull_census::core::dock_first_base == x3m::cull_small_parts::core::dock_first_base && x3m::cull_census::core::dock_second_base == x3m::cull_small_parts::core::dock_second_base &&
              x3m::cull_census::core::dock_span == x3m::cull_small_parts::core::dock_span && x3m::cull_census::core::model_offset == x3m::cull_small_parts::core::model_offset,
              "census and stub share the dock-port ranges and the model offset");
    }
    // X3M_CULL_SMALL_PROPS: only a kept row becomes culled_prop; the engine's own verdicts are never renamed.
    check(!std::strcmp(verdict_name(Verdict::culled_prop), "culled_prop") && with_prop(Verdict::kept, true) == Verdict::culled_prop &&
          with_prop(Verdict::kept, false) == Verdict::kept && with_prop(Verdict::culled_small, true) == Verdict::culled_small &&
          with_prop(Verdict::culled_size, true) == Verdict::culled_size, "census: culled_prop names only a kept node");
    {
        const std::uint32_t sorted[] = {0x100, 0x2000, 0x2040, 0x7ffffff0u, 0xfffffff0u};
        check(sorted_contains(sorted, 5, 0x100) && sorted_contains(sorted, 5, 0x2040) && sorted_contains(sorted, 5, 0xfffffff0u) &&
              !sorted_contains(sorted, 5, 0x2020) && !sorted_contains(sorted, 5, 0) && !sorted_contains(sorted, 0, 0x100),
              "census: sorted culled-prop membership");
    }
    std::printf("cull_small_parts_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
'''


# The small-prop draw skip's decision (src/proxy/cull_small_props_core.h) on a synthetic memory map: the read callback
# refuses anything outside the mapped blocks, as engine_memory::read refuses uncommitted memory.
PROPS_HARNESS = r"""
#include "cull_small_props_core.h"
#include <cstdio>
#include <cstring>
#include <vector>
using namespace x3m::cull_small_props::core;
static unsigned failures = 0;
static void check(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
// A flat 32-bit address space: base 0x10000, 64 KB, anything else unreadable.
static std::vector<unsigned char> memory(0x10000);
constexpr std::uint32_t base = 0x10000;
static bool read(std::uintptr_t at, void* out, std::size_t n) {
    if (at < base || at + n > base + memory.size()) return false;
    std::memcpy(out, memory.data() + (at - base), n);
    return true;
}
static void put(std::uint32_t at, std::uint32_t v) { std::memcpy(memory.data() + (at - base), &v, 4); }
static void text(std::uint32_t at, const char* s) { std::memcpy(memory.data() + (at - base), s, std::strlen(s) + 1); }
int main() {
    bool on = true;
    check(parse_mode("on", &on) && on && parse_mode("off", &on) && !on && parse_mode(nullptr, &on) && on &&
          !parse_mode("1", &on) && !parse_mode("ON", &on) && !parse_mode(" off", &on), "option: on|off, unset on, else refused");
    on = false;
    check(parse_mode("", &on) && on, "option: empty text is on (default on since 2026-09-29)");
    check(is_prop_name("ships\\props\\split_m1turretA_base") && is_prop_name("Ships/PROPS/weapondummy") &&
          !is_prop_name("ships\\props") && !is_prop_name("ships\\split\\split_m7_cobra\\hull") && !is_prop_name("v\\00753") &&
          !is_prop_name(nullptr), "prefix ships\\props\\ (case and slash free)");
    Box b{};
    const std::int32_t raw[7] = {65536, -131072, 0, 99, 65536, 32768, 0};
    check(part_box(raw, b) && b.lo[0] == 0.f && b.hi[0] == 2.f && b.lo[1] == -2.5f && b.hi[1] == -1.5f && b.lo[2] == 0.f && b.hi[2] == 0.f,
          "part box: centre -/+ half-extent, / 65536, the fourth word ignored");
    const std::int32_t bad[7] = {0, 0, 0, 0, 1, -1, 1};
    check(!part_box(bad, b), "part box: a negative half-extent refused");
    const std::int32_t unit[7] = {0, 0, 0, 0, 65536, 65536, 65536};
    part_box(unit, b);
    const float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 2000};
    float r = 0;
    check(screen_radius(rows, b, 5120, 1440, &r) && r > 1.28f && r < 1.282f, "radius: 1.281 px for the unit box at w 2000, 5120 wide");
    const float behind[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0.5f};
    check(!screen_radius(behind, b, 5120, 1440, &r) && !screen_radius(nullptr, b, 5120, 1440, &r) && !screen_radius(rows, b, 0, 1440, &r),
          "radius: behind the eye, no rows, no target: no size");
    // Image: body manager at 0x10100 (global 0x10000), slots at 0x10200, names at 0x10400; registry chain at 0x11000;
    // nodes at 0x12000 + 0x200 i; descriptor 0x14000 -> part 0x14100 (the engine's part box, 9x the drawn range:
    // diagnostics only). The decision takes the draw's vertex extent `ext` (the unit cube, 1.28 px).
    put(0x10000, 0x10100);
    put(0x10100 + 0xb4, 11000); put(0x10100 + 0xb8, 8); put(0x10100 + 0xbc, 0x10200);
    text(0x10400, "ships\\props\\split_m1turretB_socket");
    text(0x10440, "ships\\split\\split_m6_dragon\\hull");
    put(0x10200 + 5 * 0x1c + 0x0c, 0x10400);
    put(0x10200 + 6 * 0x1c + 0x0c, 0x10440);
    put(0x11000, 0x11100);                        // slot -> registry
    put(0x11100, 0x11200); put(0x11100 + 0x10, 1); // registry -> table, handle 1
    put(0x11200, 0x11300); put(0x11204, 2);        // table -> buckets, 2
    put(0x11300 + 4, 0x11400);                    // buckets[1] -> link
    put(0x11404, 1); put(0x11408, 0x11500);        // link: id 1, cockpit
    put(0x11500 + 0x0c, 0x11600); put(0x11500 + 0x58, 0x11700); put(0x11500 + 0x1e0, 0x11800);
    put(0x11600 + 0x70, 0x12000);                  // own object -> own root node
    put(0x11800 + 0x70, 0x12200);                  // target object -> target root node
    const auto node = [](unsigned i, std::uint32_t parent, std::uint32_t handle, std::uint32_t model) {
        const std::uint32_t n = 0x12000 + 0x200 * i;
        put(n + 0x18, parent); put(n + 0x28, handle); put(n + 0x140, model);
        return n;
    };
    const std::uint32_t own_root = node(0, 0, 0x10, 6), target_root = node(1, 0, 0x20, 6), far_root = node(2, 0, 0x30, 6);
    const std::uint32_t own_prop = node(3, own_root, 0x11, 5), target_prop = node(4, target_root, 0x21, 5);
    const std::uint32_t far_prop = node(5, far_root, 0x31, 5), far_hull = node(6, far_root, 0x32, 6);
    const std::uint32_t deep_prop = node(7, far_prop, 0x33, 5), stale_root_prop = node(8, target_root, 0x22, 5);
    put(0x14000, 0x14100);
    for (unsigned i = 0; i < 3; ++i) put(0x14100 + 0x50 + 4 * i, 9 * 65536);
    const Box unit_box{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
    const auto ext = [&unit_box]() { return &unit_box; };
    const auto none = []() -> const Box* { return nullptr; };
    unsigned asked = 0;
    const auto counted = [&]() { ++asked; return &unit_box; };
    {
        Box engine{};
        float engine_px = 0.f;
        check(Culler::engine_part_box(read, 0x14000, engine) && screen_radius(rows, engine, 5120, 1440, &engine_px) &&
              engine_px > 11.f && !Culler::engine_part_box(read, 0x1fff0, engine),
              "regression (run376): the engine part box projects to 11.6 px, the drawn range to 1.28");
    }
    Addresses a;
    a.body_global = 0x10000;
    a.cockpit_slot = 0x11000;
    static Culler c;
    c.px = 4.f;
    c.begin_frame(1);
    check(c.decide(read, a, far_prop, ext, rows, 5120, 1440) == Verdict::culled, "far prop below 4 px: culled");
    check(c.decide(read, a, deep_prop, ext, rows, 5120, 1440) == Verdict::culled, "a prop under a prop: culled");
    check(c.decide(read, a, far_hull, ext, rows, 5120, 1440) == Verdict::not_prop, "hull body: not a prop");
    check(c.decide(read, a, own_prop, ext, rows, 5120, 1440) == Verdict::exempt_own, "own ship's prop: drawn");
    check(c.decide(read, a, target_prop, ext, rows, 5120, 1440) == Verdict::exempt_target, "target's prop: drawn");
    // A freed prop address reused by another node (new handle, now under the target): re-walked, never the cached verdict.
    put(far_prop + 0x18, target_root); put(far_prop + 0x28, 0x77);
    c.begin_frame(2);
    check(c.decide(read, a, far_prop, ext, rows, 5120, 1440) == Verdict::exempt_target &&
          c.decide(read, a, stale_root_prop, ext, rows, 5120, 1440) == Verdict::exempt_target, "ancestry cache keyed on the handle");
    put(far_prop + 0x18, far_root); put(far_prop + 0x28, 0x31);
    check(c.decide(read, a, far_prop, none, rows, 5120, 1440) == Verdict::no_bounds, "extent not known: drawn");
    check(c.decide(read, a, far_hull, counted, rows, 5120, 1440) == Verdict::not_prop && asked == 0 &&
          c.decide(read, a, far_prop, counted, rows, 5120, 1440) == Verdict::culled && asked == 1,
          "the extent is asked for prop draws only");
    check(c.decide(read, a, 0, ext, rows, 5120, 1440) == Verdict::no_scope && c.decide(read, a, 0x12001, ext, rows, 5120, 1440) == Verdict::no_scope,
          "no or misaligned node: no scope");
    c.px = 1.f;
    check(c.decide(read, a, far_prop, ext, rows, 5120, 1440) == Verdict::kept_size, "1 px threshold: the 1.28 px prop is drawn");
    c.px = 4.f;
    put(0x10100 + 0xb4, 10999); c.flush_classes();
    check(c.decide(read, a, far_prop, ext, rows, 5120, 1440) == Verdict::not_prop, "a body table with the wrong fixed count: no prop");
    put(0x10100 + 0xb4, 11000); c.flush_classes();
    put(0x11100 + 0x10, 7); c.begin_frame(3); // no cockpit row for handle 7: own ship and target unknown
    check(c.decide(read, a, far_prop, ext, rows, 5120, 1440) == Verdict::unresolved, "own ship unknown: drawn (fail closed)");
    put(0x11100 + 0x10, 1);
    // evaluate: the memo shares the first draw's verdict within a frame, counts every prop draw.
    c.reset_window();
    c.begin_frame(4);
    bool first = false;
    check(c.evaluate(read, a, far_prop, ext, rows, 5120, 1440, &first) == Verdict::culled && first, "evaluate: first draw decides");
    check(c.evaluate(read, a, far_prop, ext, nullptr, 5120, 1440, &first) == Verdict::culled && !first, "evaluate: later draws share it");
    check(c.evaluate(read, a, far_hull, ext, rows, 5120, 1440) == Verdict::not_prop, "evaluate: hull");
    check(c.window.draws == 2 && c.window.culled == 2 && c.window.kept == 0 && c.window.nodes_culled == 1 && c.window.frames == 1,
          "evaluate: window counts");
    std::printf("cull_small_props_core checks_failed=%u\n", failures);
    return failures ? 1 : 0;
}
"""


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
    return probe.inspect(data, probe.decode(data), source_text(probe.CORE))


class CullSmallPartsSite(unittest.TestCase):
    def test_synthetic_image_passes_all_but_identity(self):
        report = inspect_image(image())
        checks = {k: v for k, v in report['checks'].items() if k != 'exe_identity'}
        self.assertTrue(all(checks.values()), report)
        self.assertFalse(report['checks']['exe_identity'])
        self.assertEqual(report['site_sources'], ['0x47d28c', '0x47d297'])
        self.assertEqual(report['interior_branches'], [])
        self.assertEqual(len(report['checks']), 20)

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
        self.assertEqual(probe.source_constants(source_text(probe.CORE)), probe.EXPECTED_CONSTANTS)
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
        args = (0x10000000, 0x20000000, 0x2000000c, 0x20000004, 0x20000008, 0x20000010, probe.CULL_VA, 0x10000094)
        stub = probe.encode_stub(*args)
        self.assertEqual(len(stub), 147)
        self.assertEqual(stub[:9], b'\x83\x3d' + struct.pack('<I', 0x2000000c) + b'\x00\x7e\x36')
        self.assertEqual(stub[9:21], b'\x50\xa1' + struct.pack('<I', 0x2000000c) + b'\x39\x44\x24\x30\x7d\x29')
        self.assertEqual(stub[21:32], b'\xa1' + struct.pack('<I', 0x20000000) + b'\x39\x44\x24\x30\x7c\x3a')
        self.assertEqual(stub[32:62], bytes.fromhex('8b8740010000 2d20bfb835 3d400d0300 7213 2d20b38100 3d400d0300 7207'.replace(' ', '')))
        self.assertEqual(stub[62:70], b'\x58\xff\x25' + struct.pack('<I', 0x10000094) + b'\x58')
        self.assertEqual(stub[70:82], bytes.fromhex('f787 30010000 00000020 7539'.replace(' ', '')))
        self.assertEqual(stub[82:91], b'\xff\x05' + struct.pack('<I', 0x20000010) + b'\xeb\x13\x58')
        self.assertEqual(stub[91:103], bytes.fromhex('f787 30010000 00000020 7524'.replace(' ', '')))
        self.assertEqual(stub[103:109], b'\xff\x05' + struct.pack('<I', 0x20000004))
        self.assertEqual(stub[109:134], bytes.fromhex('8b4f18 85c9 8b87d8010000 740c 8b89d8010000 3bc8 7e02 8bc1'.replace(' ', '')))
        self.assertEqual(stub[134], 0xe9)
        self.assertEqual(struct.unpack('<i', stub[135:139])[0], probe.CULL_VA - (0x10000000 + 139))
        self.assertEqual(stub[139:147], b'\xff\x05' + struct.pack('<I', 0x20000008) + b'\xeb\xac')
        off = probe.encode_stub(*args, projectiles=False)
        self.assertEqual((off[:70], off[82:91], off[103:]), (stub[:70], stub[82:91], stub[103:]))
        self.assertEqual((off[70:82], off[91:103]), (b'\xeb\x0a' + b'\xcc' * 10, b'\xeb\x0a' + b'\xcc' * 10))
        self.assertTrue(probe.projectile_stub_ok())
        with self.assertRaises(ValueError):
            probe.encode_stub(1 << 32, 0, 0, 0, 0, 0, 0, 0)
        self.assertEqual([probe.dock_model(i) for i in (901300000, 901499999, 909800000, 909999999, 901299999, 901500000, 909799999, 910000000, 0x10000)],
                         [True, True, True, True, False, False, False, False, False])
        m00 = struct.unpack('<f', struct.pack('<I', 0x3f4ccccc))[0]
        self.assertEqual((probe.threshold_for(2, m00, 1280), probe.threshold_for(4, m00, 1280), probe.threshold_for(8, m00, 1280)), (3, 6, 11))
        self.assertEqual(probe.threshold_for(2, 0.8, 1920), 2)
        self.assertEqual(probe.threshold_for(0, m00, 1280), 0)
        self.assertEqual(tuple(probe.threshold_for(px, m00, 1280, 0x3470) for px in (2, 4, 8)), (4, 7, 13))
        self.assertEqual(tuple(probe.threshold_for(px, m00, 1280, 0x4000) for px in (2, 4, 8)), (3, 6, 11))
        self.assertEqual((probe.threshold_for(2, m00, 1280, 0x105), probe.threshold_for(2, m00, 1280, 0x8001)), (0, 0))
        self.assertEqual((probe.focus_from_projection(0.5, 1.7777636), probe.focus_from_projection(m00, 3.9999745), probe.focus_from_projection(0.8, 0)), (0x3470, 0x1a38, 0))

    def test_tracked_rows_reproduce_the_census_classes(self):
        document = json.loads(source_text(ROOT / 'verification/fixtures/run131-cull-census-rows.json'))
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
                               'projectiles': None, 'dock_px': None, 'dock_requested': None})
        self.assertEqual(probe.parse_log_line('cull_small_parts requested=4 px=4 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active scope=all projectiles=marker_mismatch')['projectiles'],
                         'marker_mismatch')
        self.assertEqual(probe.parse_log_line(' cull_small_parts requested=2 px=2 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active scope=bodies')['scope'], 'bodies')
        self.assertIsNone(probe.parse_log_line('cull_small_parts requested=2 px=0 patched=0 reason=bytes_mismatch'))
        self.assertEqual(probe.parse_value_line('cull_small_parts_value px=2 m00=0.799999952 width=1280 threshold=3'), {'px': 2.0, 'm00': 0.799999952, 'width': 1280, 'threshold': 3, 'focus': None,
                                                                                                                      'source': None, 'fallback': None, 'dock_px': None, 'dock_threshold': None})
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
        # X3M_CULL_DOCK_PARTS_PX (2026-09-29): appended fields; rows without them parse as before (None).
        frame = probe.parse_frame_line('cull_small_parts_frame device=1 frame=4827 px=4 threshold=3 culled=507 m00=0.499997884 width=5120 scope=all projectiles=on exempt_bullet=0 '
                                       'focus=0x3470 source=scene fallback=none dock_px=8 dock_threshold=5 dock_culled=28')
        self.assertEqual((frame['dock_px'], frame['dock_threshold'], frame['dock_culled']), (8.0, 5, 28))
        self.assertIsNone(probe.parse_frame_line('cull_small_parts_frame device=1 frame=900 px=4 threshold=3 culled=12 m00=0.5 width=5120')['dock_culled'])
        value = probe.parse_value_line('cull_small_parts_value px=4 m00=0.5 width=5120 threshold=3 focus=0x3470 source=scene fallback=none dock_px=8 dock_threshold=5')
        self.assertEqual((value['dock_px'], value['dock_threshold']), (8.0, 5))
        row = probe.parse_log_line('cull_small_parts requested=4.0000 px=4 patched=1 reason=ok site=0x0047d2a2 cull=0x0047d2c3 write=atomic stub=0x0a100000 camera=active scope=all '
                                   'projectiles=on dock_px=8 dock_requested=8.0000')
        self.assertEqual((row['dock_px'], row['dock_requested']), (8.0, '8.0000'))

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
        source = source_text(ROOT / 'src/proxy/motion_output.cpp')

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
        self.assertIn('cull_small_parts::begin_frame();', source_text(ROOT / 'src/proxy/capture.cpp'))

    @unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed executable not present')
    def test_installed_executable(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report)


class CullSmallPropsCore(unittest.TestCase):
    def test_core_compiled(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-cull-small-props-host-') as temporary:
            directory = Path(temporary)
            (directory / 'harness.cpp').write_text(PROPS_HARNESS)
            executable = directory / 'cull_small_props_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                                    str(directory / 'harness.cpp'), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(run.stdout, 'cull_small_props_core checks_failed=0\n')

    def test_draw_path_wiring(self):
        """The skip sits after gate 2 and before the jitter, returns D3D_OK without forwarding, and reports the node to
        the census; the capture.cpp config row refuses without the pixel setting or the route."""
        motion = source_text(ROOT / 'src/proxy/motion_output.cpp')
        gate2 = motion.index('    route.scene = true;\n')
        skip = motion.index('if (props_on_ && cull_small_prop(call, route)) return;')
        jitter = motion.index('if (jitter_active_ && (shadow_.vs_row || shadow_.vs_prepass)) apply_jitter(route);')
        self.assertLess(gate2, skip)
        self.assertLess(skip, jitter)
        inc = source_text(ROOT / 'src/proxy/motion_output_cull_small_props_inc.h')
        for needle in ('route.submit = false;', 'route.submission_error = D3D_OK;', 'cull_census::note_culled_prop(',
                       'object_trace::scope_node(&descriptor, &node)', 'SetLastError(error);',
                       # run376: the size is the drawn range's extent (the shadow-replay cache, queued when missing)
                       'candidate_extents_.find(key, &stale)', 'queue_candidate_extent(key, shadow_.stream0_identity',
                       'small_prop_extent(call, extent_box)', 'log("cull_small_prop_box '):
            self.assertIn(needle, inc)
        self.assertNotIn('engine_patch', inc)  # render-only: no engine write
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        for reason in ('"invalid"', '"off"', '"no_px"', '"route_off"'):
            self.assertIn(reason, capture)
        # Default on since 2026-09-29: the row is logged on every launch, requested=unset when the variable is absent.
        block = capture[capture.index('L"X3M_CULL_SMALL_PROPS"'):capture.index('log("cull_small_props requested=')]
        self.assertNotIn('if (mode_length)', block)
        self.assertIn('mode_length ? mode : "unset"', capture)
        self.assertIn('configure_cull_small_props(cull_small_props_on, cull_small_props_px)', capture)


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
                code, output, _ = self.launch(directory, *args, inherited={'X3M_CULL_SMALL_PARTS_PX': '2',
                                                                          'X3M_CULL_SMALL_PARTS_PROJECTILES': 'off'})
                self.assertEqual(code, 0)
                self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', json.loads(output)['env'])
                self.assertNotIn('X3M_CULL_SMALL_PARTS_PROJECTILES', json.loads(output)['env'])

    def test_dry_run_carries_the_value(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory)[1])
            code, output, error = self.launch(directory, '--cull-small-parts', '2')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual(delivered['command'], baseline['command'])
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']},
                             {'X3M_CULL_SMALL_PARTS_PX': '2.0000', 'X3M_CULL_SMALL_PARTS_PROJECTILES': 'on', 'X3M_CULL_DOCK_PARTS_PX': '8.0000'})

    def test_scope_option_removed_and_inherited_scope_dropped(self):
        # --cull-small-parts-scope (the `bodies` A/B) was removed on 2026-09-25: the stub always culls every node.
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = self.launch(directory, '--cull-small-parts', '2', '--cull-small-parts-scope', 'all')
            self.assertEqual(code, 2)
            self.assertIn('unrecognized arguments', error)
            code, output, error = self.launch(directory, '--cull-small-parts', '2', inherited={'X3M_CULL_SMALL_PARTS_SCOPE': 'bodies'})
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_CULL_SMALL_PARTS_SCOPE', json.loads(output)['env'])

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

    def test_modded_launch_defaults_to_four_px(self):
        """Run 43 B default at 2 px, raised to 4 px on 2026-09-25 (stand-command promotion): every modded
        launch culls over all nodes; an explicit 0 is the off switch and --vanilla forwards nothing."""
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.modded_launch(directory)
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_CULL_SMALL_PARTS_PX'], '4.0000')
            self.assertNotIn('X3M_CULL_SMALL_PARTS_SCOPE', env)
            # An explicit value still wins.
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '2')[1])['env']
            self.assertEqual(env['X3M_CULL_SMALL_PARTS_PX'], '2.0000')
            # Explicit off, even with the variable inherited.
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '0', inherited={'X3M_CULL_SMALL_PARTS_PX': '8'})[1])['env']
            self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', env)
            # --vanilla sets nothing.
            self.assertNotIn('X3M_CULL_SMALL_PARTS_PX', json.loads(self.launch(directory)[1])['env'])

    def test_small_props_sent_only_when_given(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('on', 'off'):
                code, output, error = self.modded_launch(directory, '--cull-small-props', value)
                self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_CULL_SMALL_PROPS'], value)
            # Not given: not sent (the DLL default is on since 2026-09-29), and an inherited value is dropped.
            code, output, error = self.modded_launch(directory, inherited={'X3M_CULL_SMALL_PROPS': 'on'})
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_CULL_SMALL_PROPS', json.loads(output)['env'])
            for args in (('--cull-small-parts', '0', '--cull-small-props', 'on'),):
                code, _, error = self.modded_launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--cull-small-props requires a non-zero --cull-small-parts', error)
            code, _, error = self.launch(directory, '--cull-small-props', 'on')  # --vanilla: no cull
            self.assertEqual(code, 2)
            code, _, error = self.modded_launch(directory, '--cull-small-props', '1')
            self.assertEqual(code, 2)
            self.assertIn('invalid choice', error)
            self.assertIn('DLL default on since 2026-09-29', source_text(ROOT / 'tools/manage.py'))

    def test_dock_parts_default_explicit_off_and_refusals(self):
        """X3M_CULL_DOCK_PARTS_PX (2026-09-29): 8 px with every non-zero cull unless given; 0 sends nothing; refused without
        the cull and out of range; an inherited value never survives."""
        with tempfile.TemporaryDirectory() as directory:
            env = json.loads(self.modded_launch(directory, inherited={'X3M_CULL_DOCK_PARTS_PX': '3'})[1])['env']
            self.assertEqual(env['X3M_CULL_DOCK_PARTS_PX'], '8.0000')
            env = json.loads(self.modded_launch(directory, '--cull-dock-parts', '12')[1])['env']
            self.assertEqual(env['X3M_CULL_DOCK_PARTS_PX'], '12.0000')
            env = json.loads(self.modded_launch(directory, '--cull-dock-parts', '0', inherited={'X3M_CULL_DOCK_PARTS_PX': '8'})[1])['env']
            self.assertNotIn('X3M_CULL_DOCK_PARTS_PX', env)
            env = json.loads(self.modded_launch(directory, '--cull-small-parts', '0', inherited={'X3M_CULL_DOCK_PARTS_PX': '8'})[1])['env']
            self.assertNotIn('X3M_CULL_DOCK_PARTS_PX', env)
            self.assertNotIn('X3M_CULL_DOCK_PARTS_PX', json.loads(self.launch(directory, inherited={'X3M_CULL_DOCK_PARTS_PX': '8'})[1])['env'])
            for args in (('--cull-small-parts', '0', '--cull-dock-parts', '8'),):
                code, _, error = self.modded_launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--cull-dock-parts requires a non-zero --cull-small-parts', error)
            for value in ('65', '-1', 'nan'):
                code, _, error = self.modded_launch(directory, '--cull-dock-parts', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--cull-dock-parts out of range', error)

    def test_out_of_range_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('65', '-1', 'nan', '1e-7'):
                code, _, error = self.launch(directory, '--cull-small-parts', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--cull-small-parts out of range', error)


if __name__ == '__main__':
    unittest.main()
