"""Host checks of the weapon-effects classifier and ring (src/proxy/weapon_effects_core.h), compiled on the host.

The classifier (style, calibre band, tint source) run over the stock AP census through the production header must give
the [M] table of docs/architecture/weapon-effects.md section 4.4, pinned against
verification/results/weapon-effects-census/classify_preview_out.txt; the ring/overflow/retire and the game-time age
law; the stage builder of src/proxy/weapon_effects_stage.h (owner validation, the dying bit, retirement, the game-time
age filter, the decal fallback, the hit ordering and the cell map, one draw per anchor region, the cap bounding every
lit cell)
over a fake engine memory; the 5.1.1 shield colour bands, the patch tier tables and the record lifetime; Present's ownership latch
(core::stage_owned) and the ring's scope (core::ScopeLatch). No Wine, no game. Builds a small harness against the
headers with a host C++ compiler (a host-only d3d9.h declares the few types weapon_effects_pass.h names).
"""
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CENSUS = ROOT / 'verification/results/weapon-effects-census'
PREVIEW = CENSUS / 'classify_preview_out.txt'

D3D9_STUB = r'''
#pragma once
// Host-only declarations for the stage builder test (weapon_effects_pass.h's types), not a runtime substitute.
#include <cstdint>
typedef unsigned int UINT;
typedef long HRESULT;
typedef int D3DFORMAT;
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
struct D3DCAPS9 {};
struct IDirect3DDevice9;
struct IDirect3DTexture9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DSurface9;
#define D3DFMT_A16B16G16R16F ((D3DFORMAT)113)
'''

HARNESS = r'''
#include "weapon_effects_core.h"
#include "weapon_effects_stage.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace core = x3m::weapon_effects::core;
namespace st = x3m::weapon_effects::stage;
static long I(const char* s) { return std::strtol(s, nullptr, 0); }
// A fake engine memory at 0x100000..0x200000 (32-bit addresses, as the records carry): object k at base + k * 0x400,
// its node at + 0x200. The reader refuses anything outside, as engine_memory::read refuses unreadable memory.
static unsigned char mem[0x100000];
constexpr std::uint32_t base = 0x100000;
static bool rd(std::uintptr_t a, void* out, std::size_t n) {
    if (a < base || a + n > base + sizeof mem) return false;
    std::memcpy(out, mem + (a - base), n);
    return true;
}
static void put(std::uint32_t a, std::uint32_t v) { std::memcpy(mem + (a - base), &v, 4); }
static std::uint32_t obj(unsigned k) { return base + k * 0x400; }
static std::uint32_t node(unsigned k) { return obj(k) + 0x200; }
// Owner k at engine (0, 0, z) with radius r (engine units), identity rows, handle 0xd000 + k.
static void owner(unsigned k, std::int32_t z, std::int32_t r) {
    std::memset(mem + (obj(k) - base), 0, 0x400);
    put(obj(k) + core::layout::obj_node, node(k));
    put(obj(k) + core::layout::obj_radius, std::uint32_t(r));
    put(node(k) + core::layout::node_handle, 0xd000 + k);
    put(node(k) + core::layout::node_pos + 8, std::uint32_t(z));
    for (unsigned i = 0; i < 3; ++i) put(node(k) + core::layout::node_rot + 16 * i + 4 * i, 0x10000);
}
// A stage-owned shield hit on owner k at local (px, 0, pz) (engine units), full shields, a fighter.
static core::Event* hit(core::Ring& ring, unsigned k, std::uint32_t t0, std::int32_t px, std::uint16_t shield = 0xffff,
                        std::int32_t pz = -20000) {
    core::Event* e = core::ring_acquire(ring);
    e->role = core::role_shield_hit;
    e->flags = std::uint8_t(core::flag_stage | core::flag_forwarded | core::flag_shield_read);
    e->t0_game_ms = t0;
    e->lifetime_ms = core::patch_lifetime_ms;
    e->damage = 1200;
    e->tint_rgb = 0x1000000u | 255u;
    e->alt_rgb332 = 0;
    e->dir[0] = e->dir[1] = e->dir[2] = 0; // no travel: the radial region
    e->owner = obj(k);
    e->node = node(k);
    e->handle = 0xd000 + k;
    e->shield_q = shield;
    e->patch_tier = core::patch_fighter;
    e->tier_source = core::tier_by_station; // the hull decal's cases (the bubble's: bubble_cases)
    e->pos[0] = px;
    e->pos[1] = 0;
    e->pos[2] = pz;
    e->radius = 20000;
    return e;
}
static unsigned live(const core::Ring& r) {
    unsigned n = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) n += r.slots[i].sequence != 0;
    return n;
}
static int stage_cases() {
    static core::Ring ring;
    x3m::renderer::WeaponPatch patches[core::max_patches];
    x3m::renderer::WeaponDecal decals[core::max_decals];
    st::Camera cam;
    cam.r[0] = cam.r[4] = cam.r[8] = 1.f;
    auto build = [&](std::uint32_t now) {
        return st::build(ring, now, &rd, cam, st::Tuning{}, patches, core::max_patches, decals, core::max_decals);
    };
    const std::uint32_t L = core::patch_lifetime_ms;
    // 1 a live owner: one hit on its front (0, 0, -200): one draw per face on the hit's side of each axis (-z) and on
    // both sides of the axes whose plane holds it (+-x, +-y: seeds 1..5), its colour (full shields: light blue); the -z
    // face's draw: the ring hit, its seven footprint cells (the hit cell lit at 0.1 s, its ring of six
    // at 0.2 s), every cell anchored at the hit's depth along -z (200), the extent (the outermost cell centres plus two
    // circumradii) and the box: the cells' hexagons in the plane, the anchors -+ the extent along the axis.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0);
    st::Counts n = build(1100);
    const auto& p = patches[0];
    unsigned lit = 0, off_hit = 0;
    for (unsigned i = 0; i < p.cells; ++i) {
        lit += p.cell_since[i] >= 0.f;
        off_hit += p.cell_anchor[i] != 200.f;
    }
    std::printf("live patches=%u hits=%u retired=%u decals=%u patch_hits=%u age=%.3f centre_z=%.1f anchor=%.1f box_u=%.3f box_v=%.3f box_a0=%.3f box_a1=%.3f radius=%.2f reach=%.2f extent=%.4f cells=%u lit=%u off_hit=%u cell=%.2f seed0=%.0f seed1=%.0f seed2=%.0f hit_x=%.3f hit_y=%.3f r=%.3f g=%.3f b=%.3f axis_z=%.0f\n",
                n.patches, n.hits, n.retired, n.decals, p.hits, p.age_s[0], p.centre[2], p.cell_anchor[0], p.box[3], p.box[4],
                p.box[2], p.box[5],
                p.radius, p.reach, p.extent, p.cells, lit, off_hit, p.cell, patches[0].seed, patches[1].seed,
                patches[2].seed, p.hit[0][0], p.hit[0][1], p.colour[0], p.colour[1], p.colour[2], p.axes[2][2]);
    // 2..5 validation: a replaced node, a changed handle, the dying bit, an unreadable owner; each retires every record
    // of that owner (three hits) and leaves another owner's hit alone (its three lattices draw).
    const char* names[4] = {"node_mismatch", "handle_mismatch", "dying", "unreadable"};
    for (unsigned c = 0; c < 4; ++c) {
        core::ring_clear(ring);
        ring.retired = 0; // the running counter (ring_clear keeps it for the period rows)
        owner(0, 40000, 20000);
        owner(1, 40000, 20000);
        for (unsigned i = 0; i < 3; ++i) hit(ring, 0, 1000 + i, 0);
        hit(ring, 1, 1000, 0);
        if (c == 0) put(obj(0) + core::layout::obj_node, node(2));
        if (c == 1) put(node(0) + core::layout::node_handle, 0xbad);
        if (c == 2) put(obj(0) + core::layout::obj_flags, core::layout::obj_dying);
        if (c == 3)
            for (core::Event& e : ring.slots)
                if (e.sequence && e.owner == obj(0)) e.owner = 0x10;
        n = build(1100);
        std::printf("%s patches=%u retired=%u live=%u ring_retired=%u\n", names[c], n.patches, n.retired, live(ring), ring.retired);
    }
    // 6 the age filter in game time: L - 400 ms drawn (the slowest cells still fading), L + 1 skipped (left for
    // Present), a future t0 (a load) skipped.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0);
    const unsigned before = build(1000 + L - 400).patches, after = build(1000 + L + 1).patches, future = build(900).patches;
    std::printf("age lifetime=%u before=%u after=%u future=%u live=%u\n", L, before, after, future, live(ring));
    // 7 the decal fallback: an owner without an ellipsoid (radius 0) gets a flat patch at the hit point, no patch.
    core::ring_clear(ring);
    owner(0, 400000, 0);
    hit(ring, 0, 1000, 0)->pos[2] = -200000;
    n = build(1100);
    std::printf("decal patches=%u decals=%u centre_z=%.1f radius=%.2f cell=%.2f age=%.3f\n", n.patches, n.decals,
                decals[0].centre[2], decals[0].radius, decals[0].cell, decals[0].age_s);
    // 8 the hit ordering: five hits on one owner aged 300, 100, 500, 50, 200 ms (ring order): on the z lattice one draw,
    // the ring/core slots the newest four (younger than the ring's travel) in age order, every hit's footprint in the cell
    // map (unique keys); the newest hit (half shields) colours it.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    const std::uint32_t now = 2000;
    const std::uint32_t ages[5] = {300, 100, 500, 50, 200};
    for (unsigned i = 0; i < 5; ++i) hit(ring, 0, now - ages[i], std::int32_t(1000 * (i + 1)), i == 3 ? 0x8000 : 0xffff);
    n = build(now);
    const auto& f = patches[0];
    unsigned dupes = 0;
    for (unsigned i = 0; i < f.cells; ++i)
        for (unsigned j = i + 1; j < f.cells; ++j) dupes += f.cell_key[i] == f.cell_key[j];
    std::printf("four patches=%u hits=%u patch_hits=%u cells=%u dupes=%u age0=%.3f age1=%.3f age2=%.3f age3=%.3f r=%.3f g=%.3f b=%.3f\n",
                n.patches, n.hits, f.hits, f.cells, dupes, f.age_s[0], f.age_s[1], f.age_s[2], f.age_s[3], f.colour[0],
                f.colour[1], f.colour[2]);
    // The cells sit at their hits: on the -z face every cell's centre (plane) within the footprint (reach plus the hit
    // cell's circumradius) of one of the five hits' projections (x, 0), each anchored on the plane (depth 200), inside
    // the draw's box.
    {
        unsigned off = 0, bad = 0, outside = 0;
        for (unsigned i = 0; i < f.cells; ++i) {
            const std::uint32_t packed = std::uint32_t(f.cell_key[i]);
            const float cx = 0.5f * float(int(packed >> 12) - 2048) * f.cell, cy = 0.8660254f * float(int(packed & 0xfff) - 2048) * f.cell;
            float best = 1e30f;
            for (unsigned h = 0; h < 5; ++h) {
                const float d = std::sqrt((cx - 10.f * float(h + 1)) * (cx - 10.f * float(h + 1)) + cy * cy);
                best = d < best ? d : best;
            }
            off += best > f.reach + 0.5774f * f.cell + 1e-3f;
            bad += f.cell_anchor[i] != 200.f;
            outside += cx < f.box[0] || cx > f.box[3] || cy < f.box[1] || cy > f.box[4] || f.cell_anchor[i] < f.box[2] ||
                       f.cell_anchor[i] > f.box[5];
        }
        std::printf("at_hits cells=%u off=%u bad_anchor=%u outside_box=%u\n", f.cells, off, bad, outside);
    }
    // 9 a hit on the front and one on the back of the same owner: both on every face (review F1), the +z face's box
    // turned away from the eye culled: five draws; on -z both project to the same cells, anchored at the newer hit.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0, 0xffff, -20000);
    hit(ring, 0, 1010, 0, 0xffff, 20000);
    n = build(1100);
    std::printf("regions patches=%u hits=%u culled=%u seed0=%.0f anchor0=%.1f seed1=%.0f\n", n.patches, n.hits,
                n.faces_culled, patches[0].seed, patches[0].cell_anchor[0], patches[1].seed);
    // 9b the face cull on boxes of a face whose axis is the view's +z (the eye at the origin): a box straight ahead
    // (every corner within 6 degrees of the axis, the axis pointing away from the eye) is culled; one spanning 11 to 45
    // degrees (across the cut at 32.7) and one at 45 to 63 degrees are drawn; the cut's sine against the header.
    {
        x3m::renderer::WeaponPatch b{};
        auto culled = [&](float u0, float u1, float a0, float a1) {
            const float box[6] = {u0, -1.f, a0, u1, 1.f, a1};
            std::memcpy(b.box, box, sizeof box);
            return unsigned(st::face_culled(b));
        };
        std::printf("cull ahead=%u straddle=%u aside=%u sin=%.3f\n", culled(-10.f, 10.f, 100.f, 120.f),
                    culled(20.f, 100.f, 100.f, 102.f), culled(100.f, 200.f, 100.f, 102.f), core::face_cull_sin);
    }
    // 9c the screen-area budget (review F4): ten owners each hit on its front 30 m from the eye (every face box a
    // whole screen) with the projection set: past 8 screens the draws of the owners whose newest hit is the oldest go
    // (area_dropped), the kept boxes sum to at most the budget, every kept draw newer than every dropped one.
    {
        core::ring_clear(ring);
        st::Camera pc = cam;
        pc.m00 = 1.6f;
        pc.m11 = 2.84f;
        for (unsigned k = 0; k < 10; ++k) {
            owner(k, 20000, 20000); // its front (local z -150) at view z 50: the boxes fill the screen
            put(node(k) + core::layout::node_pos, std::uint32_t(std::int32_t(k) * 100));
            hit(ring, k, 1000 + 20 * k, 0, 0xffff, -15000);
        }
        st::Tuning bt{};
        bt.near_z = 1.f;
        const st::Counts m = st::build(ring, 1300, &rd, pc, bt, patches, core::max_patches, decals, core::max_decals);
        float area = 0.f, kept_oldest = 0.f;
        for (unsigned i = 0; i < m.patches; ++i) {
            area += st::box_screens(patches[i], pc, bt.near_z);
            kept_oldest = patches[i].newest_s > kept_oldest ? patches[i].newest_s : kept_oldest;
        }
        std::printf("budget patches=%u area_dropped=%u area=%.3f budget=%.1f kept_oldest=%.3f\n", m.patches, m.area_dropped,
                    area, core::screen_budget, kept_oldest);
        core::ring_clear(ring);
    }
    // 10 records the stage does not own and hull hits never draw.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0)->flags = core::flag_forwarded;
    hit(ring, 0, 1000, 0)->role = core::role_hull_hit;
    n = build(1100);
    std::printf("skipped patches=%u hits=%u live=%u\n", n.patches, n.hits, live(ring));
    // 12 a hit whose cells have all faded (age 2.9 s: past the slowest cell, inside the 2.941 s lifetime) draws
    // nothing and is no drop.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0);
    n = build(1000 + 2900);
    std::printf("faded patches=%u dropped=%u hits=%u\n", n.patches, n.dropped, n.hits);
    // 13 the owner window: 70 live hits on one owner, the newest owner_hits feed the maps, the rest are counted.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    for (unsigned i = 0; i < 70; ++i) hit(ring, 0, 1000 + 10 * i, std::int32_t(200 * (i % 7)));
    n = build(1000 + 10 * 70);
    std::printf("window hits=%u hits_dropped=%u owner_hits=%u\n", n.hits, n.hits_dropped, core::owner_hits);
    // 14 a full face: 20 disjoint fighter footprints (7 cells each, 140 > 128, four cells apart along x) on a large
    // owner's front, ages 0.55 .. 1.5 s: on -z, +y and -y 140 cells each, on +x one footprint (the hits share their
    // (y, z)), on -x the first hit's. One draw per face: the 128 cells with the newest clocks stay, the oldest 12 (the
    // oldest hit's 7 and 5 of the next) are dropped on -z, +y and -y; the default spills those into second draws and
    // drops nothing, every cell once per face.
    {
        auto fill = [&](unsigned draws) {
            core::ring_clear(ring);
            owner(0, 4000000, 2000000);
            for (unsigned i = 0; i < 20; ++i) hit(ring, 0, 1000 + 50 * i, std::int32_t(6000 * i), 0xffff, -200000);
            st::Tuning t{};
            t.region_draws = draws;
            return st::build(ring, 1000 + 50 * 20 + 500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals);
        };
        n = fill(1);
        float kept_max = 0.f;
        for (unsigned i = 0; i < patches[0].cells; ++i) kept_max = patches[0].cell_since[i] > kept_max ? patches[0].cell_since[i] : kept_max;
        std::printf("evict patches=%u cells=%u cells_dropped=%u spills=%u kept_max=%.4f oldest=%.4f\n", n.patches,
                    patches[0].cells, n.cells_dropped, n.spills, kept_max, 1.5f);
        n = fill(st::region_draws);
        unsigned total = 0, dupes = 0;
        for (unsigned a = 0; a < n.patches; ++a) total += patches[a].cells;
        for (unsigned a = 0; a < n.patches; ++a)
            for (unsigned i = 0; i < patches[a].cells; ++i)
                for (unsigned b = a; b < n.patches; ++b)
                    for (unsigned j = (b == a ? i + 1 : 0); j < patches[b].cells; ++j)
                        dupes += patches[a].seed == patches[b].seed && patches[a].cell_key[i] == patches[b].cell_key[j];
        std::printf("spill patches=%u cells=%u cells_dropped=%u spills=%u dupes=%u hits0=%u hits1=%u seed1=%.0f\n",
                    n.patches, total, n.cells_dropped, n.spills, dupes, patches[0].hits, patches[1].hits, patches[1].seed);
    }
    // 16 Event::dir is not used: the same hit with no travel and with travels along +x and -y gives identical draws.
    {
        float keys[3][3 * 128];
        unsigned counts[3] = {};
        for (unsigned v = 0; v < 3; ++v) {
            core::ring_clear(ring);
            owner(0, 40000, 20000);
            core::Event* e = hit(ring, 0, 1000, 3000);
            if (v == 1) e->dir[0] = 127;
            if (v == 2) e->dir[1] = -127;
            n = build(1300);
            for (unsigned d = 0; d < n.patches && d < 3; ++d)
                for (unsigned i = 0; i < patches[d].cells; ++i) keys[v][counts[v]++] = patches[d].cell_key[i] + patches[d].cell_since[i];
        }
        bool same = counts[0] == counts[1] && counts[0] == counts[2];
        for (unsigned i = 0; same && i < counts[0]; ++i) same = keys[0][i] == keys[1][i] && keys[0][i] == keys[2][i];
        std::printf("dir_unused identical=%u cells=%u\n", unsigned(same), counts[0]);
    }
    // 16b the y faces (Run236: hits from above and below): a hit on the top (0, 20000, 0) lights +y (frame (u, v, a) =
    // (z, x, y)), one on the bottom -y ((z, -x, -y)), each at the plane point (0, 0), the table entry at y = +-200.
    for (int side = 1; side >= -1; side -= 2) {
        core::ring_clear(ring);
        owner(0, 40000, 20000);
        core::Event* e = hit(ring, 0, 1000, 0, 0xffff, 0);
        e->pos[1] = side * 20000;
        n = build(1100);
        unsigned yi = 0; // the y face on the hit's side (+z is culled: it faces away from the eye)
        while (yi + 1 < n.patches && patches[yi].seed != (side > 0 ? 4.f : 5.f)) ++yi;
        const auto& q = patches[yi];
        std::printf("%s patches=%u seed=%.0f a=%.0f,%.0f,%.0f u=%.0f,%.0f,%.0f v=%.0f,%.0f,%.0f view_a=%.0f,%.0f,%.0f "
                    "anchor=%.1f hit_u=%.3f hit_v=%.3f cells=%u\n",
                    side > 0 ? "ytop" : "ybottom", n.patches, q.seed, q.anchor[0], q.anchor[1], q.anchor[2], q.anchor_u[0],
                    q.anchor_u[1], q.anchor_u[2], q.anchor_v[0], q.anchor_v[1], q.anchor_v[2], q.view_a[0], q.view_a[1],
                    q.view_a[2], q.cell_anchor[0], q.hit[0][0], q.hit[0][1], q.cells);
    }
    // 16c the own ship in the cockpit view (stage::dim_own_cockpit): a patch of owner 0 dimmed to own_cockpit_gain in
    // the cockpit, unchanged in the chase view, for another own node, or without an own node.
    {
        core::ring_clear(ring);
        owner(0, 40000, 20000);
        hit(ring, 0, 1000, 0);
        float ratio[4];
        const std::uint32_t own[4] = {node(0), node(0), node(1), 0u};
        const bool view[4] = {true, false, true, true};
        unsigned dimmed[4];
        for (unsigned v = 0; v < 4; ++v) {
            n = build(1100);
            const float before = patches[0].colour[2];
            dimmed[v] = st::dim_own_cockpit(patches, n.patches, own[v], view[v]);
            ratio[v] = patches[0].colour[2] / before;
        }
        std::printf("cockpit own=%.4f chase=%.4f other=%.4f none=%.4f dimmed=%u%u%u%u gain=%.4f node=%u\n", ratio[0],
                    ratio[1], ratio[2], ratio[3], dimmed[0], dimmed[1], dimmed[2], dimmed[3], core::own_cockpit_gain,
                    unsigned(patches[0].owner_node == node(0)));
        // The cockpit view's freshness (review F2): a visit this frame, and the next frame without one, count; two
        // frames without a visit are stale (no dim); a new visit counts again; another view mode never.
        st::CockpitClock clk;
        const bool f0 = clk.cockpit(5, 1, 100), f1 = clk.cockpit(5, 1, 101), f2 = clk.cockpit(5, 1, 102);
        const bool f3 = clk.cockpit(6, 1, 103), f4 = clk.cockpit(7, 2, 104);
        std::printf("clock fresh=%u previous=%u stale=%u revisit=%u chase=%u\n", unsigned(f0), unsigned(f1), unsigned(f2),
                    unsigned(f3), unsigned(f4));
    }
    // 11 the shield colour bands (5.1.1) and the patch tier tables.
    const float fr[7] = {0.9f, 0.75f, 0.6f, 0.5f, 0.4f, 0.35f, 0.2f};
    std::printf("colour");
    for (float x : fr) {
        float c[3];
        core::shield_colour(x, core::ShieldBands{}, c);
        std::printf(" f%02.0f=%.4f,%.4f,%.4f", double(x) * 100.0, c[0], c[1], c[2]);
    }
    std::printf("\n");
    std::printf("tiers m3=%u m5=%u m6=%u ts=%u m7=%u m2=%u tl=%u unknown=%u r10m=%u r50m=%u r200m=%u\n",
                core::patch_tier_of_ship(2023), core::patch_tier_of_ship(2025), core::patch_tier_of_ship(2026),
                core::patch_tier_of_ship(2031), core::patch_tier_of_ship(2027), core::patch_tier_of_ship(2022),
                core::patch_tier_of_ship(2032), core::patch_tier_of_ship(9999), core::patch_tier_of_radius(5000),
                core::patch_tier_of_radius(25000), core::patch_tier_of_radius(100000));
    return 0;
}

// The ship routing and the eye sphere's geometry (5.1.1 "Placement", user decision 2026-11-12 on FL 69 A: the runtime
// hull walk removed; weapon_effects_lathe.h, weapon_effects_bubble.h) over the same fake memory: a TShips type table of
// eight rows at base + 0x40000 with its slots (table, count) at base + 0x48000, and a profiles block.
static void put16(std::uint32_t a, std::uint16_t v) { std::memcpy(mem + (a - base), &v, 2); }
static std::uint32_t type_row(unsigned r) { return base + 0x40000 + r * core::layout::type_stride; }
constexpr std::uint32_t type_slots = base + 0x48000;
static core::ShipProfiles profiles;
// A box-like profile of TShips row `row`, class `cls`: z in [-20000, 20000] scene units, every slice centred with
// half-widths hx, hy (scene units = engine units at unit axis scales).
static void add_profile(std::int32_t row, std::int32_t cls, float hx, float hy) {
    core::ShipProfile& p = profiles.entries[profiles.count];
    p.row = row;
    p.class_id = cls;
    p.z0 = -20000.f;
    p.z1 = 20000.f;
    for (unsigned i = 0; i < core::profile_slices; ++i) {
        p.slice[i][0] = p.slice[i][1] = 0.f;
        p.slice[i][2] = hx;
        p.slice[i][3] = hy;
    }
    profiles.slot_of[row] = std::uint16_t(++profiles.count);
}
// Owner k as a ship (class 7) of TShips row `row` whose type record carries class id `cls`; unit root axis scales.
static void ship(unsigned k, std::int16_t row, std::int32_t cls) {
    put16(obj(k) + core::layout::obj_class, core::layout::class_ship);
    put16(obj(k) + core::layout::obj_subtype, std::uint16_t(row));
    for (unsigned i = 0; i < 3; ++i) put(node(k) + core::layout::node_axis_scale + 4 * i, 0x10000);
    put(type_row(unsigned(row)) + core::layout::ship_class_id, std::uint32_t(cls));
}
static int bubble_cases() {
    static core::Ring ring;
    static x3m::renderer::WeaponPatch patches[core::max_patches];
    x3m::renderer::WeaponDecal decals[core::max_decals];
    st::Camera cam;
    cam.r[0] = cam.r[4] = cam.r[8] = 1.f;
    // 1 the mapping onto a bubble of centre (0, -5, 10) and half-extents 1.15 (60, 10, 20) view units: a point on the
    // hull ellipsoid's nose maps to the bubble's nose (B = c + e Q, on the bubble: |(B - c) / e| = 1, along the radial
    // line of q); a flank point to its flank; the centre takes +z.
    st::Bubble bb;
    bb.c[0] = 0.f;
    bb.c[1] = -5.f;
    bb.c[2] = 10.f;
    bb.e[0] = 1.15f * 60.f;
    bb.e[1] = 1.15f * 10.f;
    bb.e[2] = 1.15f * 20.f;
    {
        const float nose[3] = {0.f, -5.f, 10.f + 20.f}, flank[3] = {60.f, -5.f, 10.f}, mid[3] = {0.f, -5.f, 10.f};
        const float tilted[3] = {30.f, 0.f, 25.f};
        float B[3], Q[3];
        st::bubble_point(bb, nose, B, Q);
        std::printf("nose bx=%.4f by=%.4f bz=%.4f\n", B[0], B[1], B[2]);
        st::bubble_point(bb, flank, B, Q);
        std::printf("flank bx=%.4f by=%.4f bz=%.4f\n", B[0], B[1], B[2]);
        st::bubble_point(bb, mid, B, Q);
        std::printf("centre bx=%.4f by=%.4f bz=%.4f\n", B[0], B[1], B[2]);
        st::bubble_point(bb, tilted, B, Q);
        float r2 = 0.f, q0[3];
        for (unsigned i = 0; i < 3; ++i) {
            const float qq = (B[i] - bb.c[i]) / bb.e[i];
            r2 += qq * qq;
            q0[i] = (tilted[i] - bb.c[i]) / bb.e[i];
        }
        // the radial line: (B - c) / e parallel to q (their cross product's length)
        const float bq[3] = {(B[0] - bb.c[0]) / bb.e[0], (B[1] - bb.c[1]) / bb.e[1], (B[2] - bb.c[2]) / bb.e[2]};
        const float cx = bq[1] * q0[2] - bq[2] * q0[1], cy = bq[2] * q0[0] - bq[0] * q0[2], cz = bq[0] * q0[1] - bq[1] * q0[0];
        std::printf("tilted on_bubble=%.6f radial=%.6f\n", r2, std::sqrt(cx * cx + cy * cy + cz * cz));
    }
    // 2 the routing through the builder: a ship with a profile of its class draws on its lathe whatever its tier; a ship
    // without one (no profile for its row, another class, a stale table) draws nothing and is counted (the hook forwards
    // it native); a station takes the hull decal; the own ship in the cockpit with a profile takes the eye sphere of its
    // profile bounds' smallest half-extent x bubble_scale, without one nothing.
    {
        put(type_slots, type_row(0));
        put(type_slots + 4, 8);
        profiles.clear();
        profiles.tships_rows = 8;
        add_profile(3, 2023, 6000.f, 1000.f);   // an M3: 60 x 10 x 200 view units
        add_profile(6, 2022, 20000.f, 20000.f); // an M2: 200 x 200 x 200 view units
        static st::LatheCache lathes;
        lathes.clear();
        st::Tuning t{};
        t.profiles = &profiles;
        t.ship_table_slot = type_slots;
        t.ship_count_slot = type_slots + 4;
        auto route = [&](const char* name, std::int16_t row, std::int32_t cls, std::uint8_t tier, std::uint8_t source,
                         bool own_cockpit) {
            core::ring_clear(ring);
            owner(0, 40000, 20000);
            if (source == core::tier_by_station)
                put16(obj(0) + core::layout::obj_class, core::layout::class_dock);
            else
                ship(0, row, cls);
            core::Event* e = hit(ring, 0, 1000, 0, 0xffff, -3000);
            e->patch_tier = tier;
            e->tier_source = source;
            st::Tuning tt = t;
            tt.cockpit = own_cockpit;
            tt.own_node = own_cockpit ? node(0) : 0;
            const st::Counts c = st::build(ring, 1500, &rd, cam, tt, patches, core::max_patches, decals, core::max_decals,
                                           nullptr, &lathes);
            unsigned lathe = 0, eye = 0, decal = 0;
            float eye_r = 0.f;
            for (unsigned i = 0; i < c.patches; ++i) {
                lathe += patches[i].lathe != nullptr;
                eye += patches[i].eye_sphere;
                decal += !patches[i].lathe && !patches[i].bubble;
                if (patches[i].eye_sphere) eye_r = patches[i].bubble_e[0];
            }
            std::printf("route_%s patches=%u lathe=%u eye=%u decal=%u lathes=%u eye_spheres=%u no_profile=%u mismatch=%u "
                        "unreadable=%u stale=%u eye_r=%.3f\n", name, c.patches, lathe, eye, decal, c.lathes, c.eye_spheres,
                        c.lathe_no_profile, c.lathe_mismatch, c.lathe_unreadable, c.lathe_stale, eye_r);
        };
        route("fighter_profile", 3, 2023, core::patch_fighter, core::tier_by_ship_class, false);
        route("fighter_none", 4, 2023, core::patch_fighter, core::tier_by_ship_class, false);
        route("capital_profile", 6, 2022, core::patch_capital, core::tier_by_ship_class, false);
        route("capital_none", 5, 2022, core::patch_capital, core::tier_by_ship_class, false);
        route("mismatch", 3, 2024, core::patch_fighter, core::tier_by_ship_class, false);
        route("station", 0, 0, core::patch_capital, core::tier_by_station, false);
        route("own_cockpit", 6, 2022, core::patch_capital, core::tier_by_ship_class, true);
        route("own_none", 4, 2023, core::patch_fighter, core::tier_by_ship_class, true);
        profiles.tships_rows = 9; // the generator's row count differs from the live one
        route("stale", 3, 2023, core::patch_fighter, core::tier_by_ship_class, false);
        profiles.tships_rows = 8;
        t.profiles = nullptr; // no profiles loaded
        route("no_table", 3, 2023, core::patch_fighter, core::tier_by_ship_class, false);
        t.profiles = &profiles;
        // review F5: three new types in one stage call: lathe_builds_per_frame (2) build, the third waits (deferred, its
        // record kept) and draws on the next call; a non-ship owner on the lathe path counts as no profile (review F4)
        add_profile(7, 2024, 6000.f, 1000.f);
        lathes.clear();
        core::ring_clear(ring);
        const std::int16_t rows[3] = {3, 6, 7};
        const std::int32_t classes[3] = {2023, 2022, 2024};
        for (unsigned k = 0; k < 3; ++k) {
            owner(k, 40000 + 10000 * std::int32_t(k), 20000);
            ship(k, rows[k], classes[k]);
            core::Event* e = hit(ring, k, 1000, 0, 0xffff, -3000);
            e->tier_source = core::tier_by_ship_class;
        }
        const std::uint32_t builds_before = lathes.builds;
        const st::Counts first = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals,
                                           nullptr, &lathes);
        const unsigned live_after_first = live(ring);
        const st::Counts second = st::build(ring, 1516, &rd, cam, t, patches, core::max_patches, decals, core::max_decals,
                                            nullptr, &lathes);
        std::printf("deferred first_lathes=%u first_deferred=%u live=%u second_lathes=%u second_deferred=%u builds=%u "
                    "per_frame=%u\n", first.lathes, first.lathe_deferred, live_after_first, second.lathes,
                    second.lathe_deferred, lathes.builds - builds_before, st::lathe_builds_per_frame);
        core::ring_clear(ring);
        owner(0, 40000, 20000);
        put16(obj(0) + core::layout::obj_class, 0x12); // a shielded object that is no ship, staged on the lathe path
        core::Event* e = hit(ring, 0, 1000, 0, 0xffff, -3000);
        e->tier_source = core::tier_by_radius;
        const st::Counts other = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals,
                                           nullptr, &lathes);
        std::printf("not_ship patches=%u no_profile=%u\n", other.patches, other.lathe_no_profile);
    }
    // 3 the eye sphere's box over its sheet (fill_bubble): a sphere of radius 100 with one hit on its front lights -z and
    // both faces of x and y; the x and y draws' cells lie on the rim of their sheet (steeper than the cut): dropped; the
    // -z draw's box spans the sheet's depths over its cells (within the sphere's front cap).
    const float centre[3] = {0.f, 0.f, 400.f}, frame[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    const float white[3] = {1.f, 1.f, 1.f};
    {
        st::Bubble sphere;
        for (unsigned k = 0; k < 3; ++k) sphere.e[k] = 100.f;
        const float hits[1][3] = {{0.f, 0.f, -100.f}}, ages[1] = {0.5f};
        st::Counts n{};
        unsigned regions_left = st::anchor_count;
        st::fill_bubble(patches, core::max_patches, &regions_left, centre, frame, core::patch_fighter, sphere, hits, ages,
                        1, white, st::Tuning{}, &n);
        std::printf("sheet patches=%u seed=%.0f a0=%.3f a1=%.3f u0=%.3f u1=%.3f\n", n.patches,
                    n.patches ? patches[0].seed : -1.f, n.patches ? patches[0].box[2] : 0.f,
                    n.patches ? patches[0].box[5] : 0.f, n.patches ? patches[0].box[0] : 0.f,
                    n.patches ? patches[0].box[3] : 0.f);
    }
    // 4 a top hit on the bubble of case 1 a hair either side of the x axis plane (review F2: Q_x = +eps, -eps and 0):
    // every face lit by all three, the same draws with the same cells and clocks (no flip of the lit faces, no cell twice).
    {
        auto draws_of = [&](float x, double* keys, unsigned* nkeys) {
            const float hits[1][3] = {{x, 5.f, 10.f}}, ages[1] = {0.5f};
            st::Counts n{};
            unsigned regions_left = st::anchor_count;
            st::fill_bubble(patches, core::max_patches, &regions_left, centre, frame, core::patch_fighter, bb, hits, ages,
                            1, white, st::Tuning{}, &n);
            *nkeys = 0;
            for (unsigned i = 0; i < n.patches; ++i)
                for (unsigned k = 0; k < patches[i].cells; ++k)
                    keys[(*nkeys)++] = double(patches[i].seed) * 1e8 + double(patches[i].cell_key[k]) + 1e-3 * double(patches[i].cell_since[k]);
            std::sort(keys, keys + *nkeys);
            unsigned seeds = 0;
            for (unsigned i = 0; i < n.patches; ++i) seeds |= 1u << unsigned(patches[i].seed);
            return std::pair<unsigned, unsigned>(n.patches, seeds);
        };
        static double kp[6 * 128 * 8], km[6 * 128 * 8], k0[6 * 128 * 8];
        unsigned np = 0, nm = 0, n0 = 0;
        const auto dp = draws_of(0.01f, kp, &np), dm = draws_of(-0.01f, km, &nm), d0 = draws_of(0.f, k0, &n0);
        bool same = np == nm && np == n0;
        for (unsigned i = 0; same && i < np; ++i) same = kp[i] == km[i] && kp[i] == k0[i];
        unsigned dupes = 0;
        for (unsigned i = 1; i < np; ++i) dupes += kp[i] == kp[i - 1];
        std::printf("axis_plane draws_plus=%u draws_minus=%u draws_zero=%u seeds_plus=%u seeds_minus=%u cells=%u "
                    "identical=%u dupes=%u\n", dp.first, dm.first, d0.first, dp.second, dm.second, np, unsigned(same), dupes);
    }
    // 5 review F6: a hit 20000 view units off the face origin along x: its +-z cells past the packed key's bound are
    // never lit and are counted (RegionStats::key_dropped).
    {
        const float hits[1][3] = {{20000.f, 0.f, -100.f}}, ages[1] = {0.5f};
        st::RegionStats rs{};
        const unsigned drawn = st::fill_region(patches, core::max_patches, centre, frame, core::patch_fighter, 0, hits,
                                               ages, 1, white, st::Tuning{}, &rs);
        std::printf("keybound patches=%u key_dropped=%u\n", drawn, rs.key_dropped);
    }
    return 0;
}
int main(int argc, char** argv) {
    if (!std::strcmp(argv[1], "bubble")) return bubble_cases();
    if (!std::strcmp(argv[1], "classify")) {
        // argv: classify flags z_over_xy_milli(or "none") shield hull r g b
        const std::uint32_t flags = (std::uint32_t)I(argv[2]);
        const bool present = std::strcmp(argv[3], "none") != 0;
        const float e = present ? float(I(argv[3])) / 1000.f : 0.f;
        const std::int32_t shield = (std::int32_t)I(argv[4]), hull = (std::int32_t)I(argv[5]);
        const std::int32_t r = (std::int32_t)I(argv[6]), g = (std::int32_t)I(argv[7]), b = (std::int32_t)I(argv[8]);
        const std::uint8_t style = core::style_from(flags, core::shape_from_elongation(e, present));
        const std::int32_t d = shield > hull ? shield : hull;
        const char* band = core::tier_name(core::tier_from(d));
        const char* tint = core::light_saturated(r, g, b) ? "light" : "body_texture";
        std::printf("%s%s %s %s\n", core::shape_name(style & core::style_shape_mask),
                    (style & core::style_burst) ? "+burst" : "", band, tint);
        return 0;
    }
    if (!std::strcmp(argv[1], "ring")) {
        core::Ring ring;
        core::ring_clear(ring);
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            core::Event* ev = core::ring_acquire(ring);
            ev->role = (i == 3) ? core::role_shield_hit : core::role_hull_hit;
            ev->flags = (i == 3) ? std::uint8_t(core::flag_stage) : std::uint8_t(core::flag_forwarded);
            ev->owner = 0x100 + (i & 7);
        }
        const std::uint32_t kept = ring.slots[3].sequence;
        core::ring_acquire(ring);
        bool survived = false;
        for (unsigned i = 0; i < core::ring_capacity; ++i) survived |= ring.slots[i].sequence == kept;
        core::ring_clear(ring);
        for (unsigned i = 0; i < 5; ++i) { core::Event* ev = core::ring_acquire(ring); ev->owner = i < 3 ? 0xaa : 0xbb; }
        unsigned retired = core::ring_retire_owner(ring, 0xaa);
        core::Event age{}; age.t0_game_ms = 1000; age.lifetime_ms = 600;
        std::printf("overflow=%u survived=%u retired=%u age=%u exp_now=%u exp_wrap=%u\n", ring.overflow,
                    unsigned(survived), retired, core::age_ms(1100, age.t0_game_ms), unsigned(core::expired(1700, age)),
                    unsigned(core::expired(500, age)));
        return 0;
    }
    if (!std::strcmp(argv[1], "evict")) {
        // The overflow order at ring_capacity (2026-11-07): records i interleave the four classes (census or staged,
        // owner drawn or not); after the stage's stamp the victims come census-hidden, census-drawn, staged-hidden,
        // staged-drawn, each oldest first.
        static core::Ring ring;
        core::ring_clear(ring);
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            core::Event* e = core::ring_acquire(ring);
            e->flags = (i & 1) ? std::uint8_t(core::flag_stage) : std::uint8_t(core::flag_forwarded);
            e->owner = (i & 2) ? 0x2000u : 0x1000u;
        }
        core::VisibleOwners drawn;
        drawn.add(0x1000u);
        drawn.add(0x1000u); // once
        core::ring_stamp_hidden(ring, drawn);
        unsigned hidden = 0, per[4] = {}, order_ok = 1, last_rank = 0;
        std::uint32_t last_seq = 0;
        for (unsigned i = 0; i < core::ring_capacity; ++i) hidden += ring.hidden[i] & core::hidden_undrawn;
        for (unsigned n = 0; n < core::ring_capacity; ++n) {
            const unsigned v = core::ring_victim(ring);
            if (v >= core::ring_capacity) { order_ok = 0; break; }
            const core::Event& e = ring.slots[v];
            const unsigned rank = ((e.flags & core::flag_stage) ? 2u : 0u) | (e.owner == 0x1000u ? 1u : 0u);
            if (rank < last_rank || (rank == last_rank && e.sequence <= last_seq)) order_ok = 0;
            last_rank = rank;
            last_seq = e.sequence;
            ++per[rank];
            core::ring_release(ring, v);
        }
        // A record acquired since the stamp is not hidden: full ring, every record hidden, one replaced by a fresh one;
        // the next overflow evicts the oldest hidden record and keeps the fresh one.
        core::ring_clear(ring);
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            core::Event* e = core::ring_acquire(ring);
            e->flags = core::flag_stage;
            e->owner = 0x2000u;
        }
        core::ring_stamp_hidden(ring, core::VisibleOwners{});
        std::uint32_t oldest = 0xffffffffu;
        unsigned oldest_slot = 0;
        for (unsigned i = 0; i < core::ring_capacity; ++i)
            if (ring.slots[i].sequence < oldest) { oldest = ring.slots[i].sequence; oldest_slot = i; }
        core::ring_release(ring, (oldest_slot + 7) % core::ring_capacity);
        core::Event* fresh = core::ring_acquire(ring);
        fresh->flags = core::flag_stage;
        fresh->owner = 0x2000u;
        const std::uint32_t fresh_seq = fresh->sequence;
        const std::uint32_t overflow0 = ring.overflow;
        core::ring_acquire(ring)->flags = core::flag_stage;
        const unsigned overflowed = ring.overflow - overflow0;
        bool fresh_kept = false, oldest_gone = true;
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            fresh_kept |= ring.slots[i].sequence == fresh_seq;
            oldest_gone &= ring.slots[i].sequence != oldest;
        }
        // The stage's drawn owners: the shield stage adds an owner in front of the camera (1), not one behind it (2), past
        // the side planes (3) or past the far plane (4).
        core::ring_clear(ring);
        owner(1, 40000, 10000);
        owner(2, -40000, 10000);
        owner(3, 40000, 10000);
        put(node(3) + core::layout::node_pos, 400000);
        owner(4, 4000000, 10000);
        for (unsigned k = 1; k <= 4; ++k) hit(ring, k, 1000, 0);
        x3m::renderer::WeaponPatch patches[core::max_patches];
        x3m::renderer::WeaponDecal decals[core::max_decals];
        st::Camera cam;
        cam.r[0] = cam.r[4] = cam.r[8] = 1.f;
        cam.m00 = 1.6f;
        cam.m11 = 1.6f * 16.f / 9.f;
        core::VisibleOwners shield_drawn;
        shield_drawn.far_z = 10000.f;
        st::build(ring, 1010, &rd, cam, st::Tuning{}, patches, core::max_patches, decals, core::max_decals,
                  &shield_drawn);
        core::ring_stamp_hidden(ring, shield_drawn);
        unsigned shield_hidden = 0;
        for (unsigned i = 0; i < core::ring_capacity; ++i)
            shield_hidden += ring.slots[i].sequence && (ring.hidden[i] & core::hidden_undrawn);
        const bool shield_ok = shield_drawn.count == 1 && shield_drawn.owner[0] == obj(1) && shield_hidden == 3;
        // Visible first in the shield stage: 40 owners with one shield hit each, 20 in view (t0 1000) and 20 behind the
        // camera with newer hits (t0 1005). Unstamped, the 16 newest are all behind (nothing drawn); after the stamp the
        // next frame draws 16 owners, all in view.
        core::ring_clear(ring);
        for (unsigned k = 1; k <= 40; ++k) {
            owner(k, k <= 20 ? 40000 : -40000, 10000);
            if (k <= 20) put(node(k) + core::layout::node_pos, std::uint32_t(std::int32_t(-24000 + 2400 * int(k - 1))));
            hit(ring, k, k <= 20 ? 1000 : 1005, 0);
        }
        static core::VisibleOwners vf;
        unsigned vf_drawn[2] = {}, vf_on_screen = 0;
        for (unsigned frame = 0; frame < 2; ++frame) {
            vf.reset(0.f);
            st::build(ring, 1010 + 16 * frame, &rd, cam, st::Tuning{}, patches, core::max_patches, decals, core::max_decals,
                      &vf);
            core::ring_stamp_hidden(ring, vf);
            vf_drawn[frame] = vf.count;
        }
        for (unsigned j = 0; j < vf.count; ++j)
            for (unsigned k = 1; k <= 20; ++k) vf_on_screen += vf.owner[j] == obj(k);
        // An on-screen owner past the cap is located through the drawn path's checks: in view while they hold; with its
        // node's handle changed it is stamped off screen (another on-screen owner past the cap stays in view).
        unsigned dropped_in_view = 0, dropped_k = 0;
        for (unsigned k = 1; k <= 20; ++k) {
            bool drawn = false;
            for (unsigned j = 0; j < vf.count; ++j) drawn |= vf.owner[j] == obj(k);
            if (drawn) continue;
            for (unsigned i = 0; i < core::ring_capacity; ++i)
                if (ring.slots[i].sequence && ring.slots[i].owner == obj(k) && !(ring.hidden[i] & core::hidden_off_screen)) {
                    ++dropped_in_view;
                    dropped_k = k;
                }
        }
        put(node(dropped_k) + core::layout::node_handle, 0xbad);
        vf.reset(0.f);
        st::build(ring, 1042, &rd, cam, st::Tuning{}, patches, core::max_patches, decals, core::max_decals, &vf);
        core::ring_stamp_hidden(ring, vf);
        unsigned bad_off = 0, others_in_view = 0;
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            if (!ring.slots[i].sequence || ring.slots[i].owner > obj(20)) continue;
            if (ring.slots[i].owner == obj(dropped_k)) bad_off += (ring.hidden[i] & core::hidden_off_screen) != 0;
            else others_in_view += !(ring.hidden[i] & core::hidden_off_screen);
        }
        std::printf("hidden=%u census_hidden=%u census_drawn=%u staged_hidden=%u staged_drawn=%u order_ok=%u "
                    "fresh_kept=%u oldest_gone=%u overflow=%u shield_drawn_ok=%u vf_frame1_drawn=%u vf_frame2_drawn=%u vf_on_screen=%u "
                    "vf_dropped_in_view=%u vf_bad_handle_off=%u vf_others_in_view=%u\n",
                    hidden, per[0], per[1], per[2], per[3], order_ok, unsigned(fresh_kept), unsigned(oldest_gone),
                    overflowed, unsigned(shield_ok), vf_drawn[0], vf_drawn[1],
                    vf_on_screen, dropped_in_view, bad_off, others_in_view);
        return 0;
    }
    if (!std::strcmp(argv[1], "stage")) return stage_cases();
    if (!std::strcmp(argv[1], "latch")) {
        core::ScopeLatch l;
        const bool first = l.changed(core::Scope{0x111, 0, false});    // the first known key: no change
        const bool unknown = l.changed(core::Scope{0, 0, false});      // an unknown read keeps the held key
        const bool sector = l.changed(core::Scope{0x222, 0, false});   // a different key
        const bool same = l.changed(core::Scope{0x222, 0, false});
        const bool loading = l.changed(core::Scope{0, 0, true});       // a load
        const bool epoch1 = l.changed(core::Scope{0x222, 5, false});   // the first known epoch
        const bool epoch2 = l.changed(core::Scope{0x222, 6, false});   // a new epoch
        std::printf("scope first=%u unknown=%u sector=%u same=%u loading=%u epoch1=%u epoch2=%u held=%llu\n", first, unknown,
                    sector, same, loading, epoch1, epoch2, (unsigned long long)l.sector_key);
        // stage_owned(armed, resolve_ok, callback_set, callback_ok)
        std::printf("owned drew=%u nothing_to_draw=%u callback_unset=%u failed_resolve=%u callback_failed=%u unarmed=%u\n",
                    core::stage_owned(true, true, true, true), core::stage_owned(true, true, false, false),
                    core::stage_owned(true, true, false, true), core::stage_owned(true, false, true, true),
                    core::stage_owned(true, true, true, false), core::stage_owned(false, true, true, true));
        return 0;
    }
    return 2;
}
'''


def preview_rows():
    """{weapon: {flags, e, dmg, style, band}} from classify_preview_out.txt's stock_ap table (the only tracked source;
    the census JSONs are untracked). The light RGB is not in the table, so tint source is covered by the hook fixture
    and the ring/tint unit checks, not re-derived here; style and calibre band are."""
    rows, in_stock = {}, False
    for line in PREVIEW.read_text().splitlines():
        if line.startswith('## stock_ap'):
            in_stock = True
            continue
        if line.startswith('## ') and in_stock:
            break
        if in_stock and line.startswith('| ') and '---' not in line and not line.startswith('| weapon'):
            cells = [c.strip() for c in line.strip().strip('|').split('|')]
            weapon, flags, zxy, dmg, style, band = cells[0], cells[1], cells[2], cells[5], cells[6], cells[7].split()[0]
            rows[weapon] = dict(flags=int(flags, 0), e=None if zxy == '-' else float(zxy), dmg=int(dmg),
                                style=style, band=band)
    return rows


class WeaponEffectsClassifier(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('a host C++ compiler is required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-weapon-core-')
        src = Path(cls.tmp.name) / 'h.cpp'
        src.write_text(HARNESS)
        stub = Path(cls.tmp.name) / 'stub'
        stub.mkdir()
        (stub / 'd3d9.h').write_text(D3D9_STUB)
        cls.exe = Path(cls.tmp.name) / 'h'
        subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(stub), '-I', str(ROOT / 'src/proxy'), str(src), '-o',
                        str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def classify(self, row):
        e = 'none' if row['e'] is None else str(int(round(row['e'] * 1000)))
        out = subprocess.run([str(self.exe), 'classify', str(row['flags']), e, str(row['dmg']), '0', '0', '0', '0'],
                             capture_output=True, text=True, check=True).stdout.strip().split()
        return out[0], out[1]  # style, band (tint needs the light RGB, not in the tracked table)

    def test_classifier_matches_section_4_4(self):
        preview = preview_rows()
        self.assertGreaterEqual(len(preview), 40)
        for weapon, want in preview.items():
            got = self.classify(want)
            self.assertEqual(got, (want['style'], want['band']),
                             f"{weapon}: core gave {got}, section 4.4 has {(want['style'], want['band'])}")

    def test_ring_overflow_retire_and_age_law(self):
        out = subprocess.run([str(self.exe), 'ring'], capture_output=True, text=True, check=True).stdout
        fields = dict(re.findall(r'(\w+)=(\d+)', out))
        self.assertEqual(fields['overflow'], '1')
        self.assertEqual(fields['survived'], '1')  # the stage-owned shield hit is never evicted first
        self.assertEqual(fields['retired'], '3')
        self.assertEqual(fields['age'], '100')     # game-time age
        self.assertEqual(fields['exp_now'], '1')
        self.assertEqual(fields['exp_wrap'], '1')   # a future t0 (a load) reads as expired

    def test_overflow_evicts_undrawn_owner_first(self):
        # 2026-11-07: the 1024-record ring's overflow order with the stage's per-slot stamp (core::ring_stamp_hidden):
        # census-only records of an undrawn owner, then the drawn owner's, then the staged records likewise, each oldest
        # first; a record acquired since the stamp is never hidden; the shield stage draws only an owner in the view
        # volume (not behind, beside or past the far plane).
        out = subprocess.run([str(self.exe), 'evict'], capture_output=True, text=True, check=True).stdout
        f = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}
        self.assertEqual(f['hidden'], 512)
        self.assertEqual((f['census_hidden'], f['census_drawn'], f['staged_hidden'], f['staged_drawn']), (256,) * 4)
        self.assertEqual(f['order_ok'], 1)
        self.assertEqual((f['fresh_kept'], f['oldest_gone'], f['overflow']), (1, 1, 1))
        self.assertEqual(f['shield_drawn_ok'], 1)
        # visible first (shield stage, cap 16): unstamped the 16 newest are behind the camera; stamped, 16 in view
        self.assertEqual((f['vf_frame1_drawn'], f['vf_frame2_drawn'], f['vf_on_screen']), (0, 16, 16))
        # the 4 on-screen owners past the cap are located in view; one whose handle no longer matches is off screen
        self.assertEqual((f['vf_dropped_in_view'], f['vf_bad_handle_off'], f['vf_others_in_view']), (4, 1, 19))

    def run_case(self, case):
        out = subprocess.run([str(self.exe), case], capture_output=True, text=True, check=True).stdout
        rows = {}
        for line in out.splitlines():
            name, _, rest = line.partition(' ')
            rows[name] = {k: float(v) for k, v in re.findall(r'(\w+)=(-?[0-9.]+)', rest)}
        return rows

    def raw_line(self, case, name):
        out = subprocess.run([str(self.exe), case], capture_output=True, text=True, check=True).stdout
        lines = [line.partition(' ')[2] for line in out.splitlines() if line.partition(' ')[0] == name]
        self.assertEqual(len(lines), 1, name)
        return lines[0]

    def ytop_frames(self):
        out = subprocess.run([str(self.exe), 'stage'], capture_output=True, text=True, check=True).stdout
        frames = {}
        for line in out.splitlines():
            name = line.partition(' ')[0]
            if name in ('ytop', 'ybottom'):
                frames[name] = tuple(re.search(r'\b%s=([-0-9,]+)' % k, line).group(1) for k in ('a', 'u', 'v', 'view_a'))
        return frames

    def test_stage_build_owner_validation_retirement_age_decal_and_slots(self):
        r = self.run_case('stage')
        live = r['live']
        self.assertEqual((live['patches'], live['hits'], live['retired'], live['decals'], live['patch_hits']), (5, 1, 0, 0, 1))
        self.assertEqual((live['seed0'], live['seed1'], live['seed2']), (1, 2, 3))  # -z, +x, -x (then +y, -y)
        self.assertAlmostEqual(live['age'], 0.1, places=3)             # 100 game ms
        self.assertAlmostEqual(live['centre_z'], 400.0, places=1)      # engine 40000 x world_scale
        self.assertAlmostEqual(live['anchor'], 200.0, places=1)        # the hit (0, 0, -20000) along -z
        self.assertEqual(live['off_hit'], 0)
        self.assertAlmostEqual(live['box_u'], 15.0 + 0.5773503 * 15.0, delta=1e-3)  # the ring's cells +- a circumradius
        self.assertAlmostEqual(live['box_v'], 0.8660254 * 15.0 + 0.5773503 * 15.0, delta=1e-3)
        self.assertAlmostEqual(live['box_a0'], 200.0 - (15.0 + 1.1547005 * 15.0), delta=1e-3)  # the anchors -+ the extent
        self.assertAlmostEqual(live['box_a1'], 200.0 + (15.0 + 1.1547005 * 15.0), delta=1e-3)
        self.assertAlmostEqual(live['cell'], 15.0, places=2)           # 3 m x 500 x 0.01 (fighter)
        self.assertAlmostEqual(live['radius'], 22.5, places=2)         # (1 ring + 0.5) cells: the ring's footprint
        self.assertAlmostEqual(live['reach'], 15.0, places=2)          # 1 ring
        self.assertAlmostEqual(live['extent'], 15.0 + 1.1547005 * 15.0, delta=1e-3)
        self.assertEqual(live['lit'], 1)                               # age 0.1: the hit cell; its ring of six at 0.2 s
        self.assertEqual(live['cells'], 7)                             # the ring hit carries its six not reached yet
        self.assertEqual(live['axis_z'], 1)                            # the owner's rows into the draw (identity)
        self.assertEqual((live['r'], live['g'], live['b']), (0.45, 0.72, 1.0))  # full shields: light blue
        for case in ('node_mismatch', 'handle_mismatch', 'dying', 'unreadable'):
            row = r[case]
            # every record of the bad owner leaves the ring (3), the other owner's hit draws (five faces) and stays
            self.assertEqual((row['patches'], row['retired'], row['live'], row['ring_retired']), (5, 3, 1, 3), case)
        a = r['age']
        self.assertEqual((a['before'], a['after'], a['future'], a['live']), (5, 0, 0, 1))
        self.assertEqual(a['lifetime'], 2941)  # 1000 / 3.333 + 2200 x 1.2, + 1
        d = r['decal']
        self.assertEqual((d['patches'], d['decals']), (0, 1))
        self.assertAlmostEqual(d['centre_z'], 2000.0, places=1)       # the hit point: 400000 - 200000 engine units
        self.assertAlmostEqual(d['radius'], 22.5, places=2)
        self.assertAlmostEqual(d['cell'], 15.0, places=2)
        f = r['four']
        self.assertEqual((f['patches'], f['hits'], f['patch_hits'], f['dupes']), (5, 5, 4, 0))
        self.assertEqual([f['age0'], f['age1'], f['age2'], f['age3']], [0.05, 0.1, 0.2, 0.3])  # newest first, 500 past the ring
        self.assertGreater(f['cells'], 7)                               # the five footprints' union, each cell once
        self.assertEqual((f['r'], f['g'], f['b']), (1.0, 0.88, 0.42))  # the newest hit: half shields, light yellow
        h = r['at_hits']                                                # every cell at the hit it carries
        self.assertEqual((h['off'], h['bad_anchor'], h['outside_box']), (0, 0, 0))
        g = r['regions']                                                # front and back: each on its own z face
        self.assertEqual((g['patches'], g['culled'], g['seed0'], g['seed1']), (5, 1, 1, 2))  # +z culled
        self.assertEqual(g['anchor0'], -200.0)   # both hits on every face: the shared cells anchor at the newer, the back
        bu = r['budget']                                 # review F4: at most 8 screens of boxes, the oldest dropped
        self.assertGreater(bu['area_dropped'], 0)
        self.assertLessEqual(bu['area'], bu['budget'] + 1e-3)
        self.assertGreater(bu['patches'], 0)
        self.assertLess(bu['kept_oldest'], 0.3 - 20 * 4.5e-3)    # the four newest owners at most (ages 0.12 .. 0.3)
        cu = r['cull']
        self.assertEqual((cu['ahead'], cu['straddle'], cu['aside']), (1, 0, 0))
        self.assertAlmostEqual(cu['sin'], 0.841, places=3)
        self.assertEqual((r['skipped']['patches'], r['skipped']['hits'], r['skipped']['live']), (0, 0, 2))
        self.assertEqual((r['faded']['patches'], r['faded']['dropped'], r['faded']['hits']), (0, 0, 1))  # review F7
        self.assertEqual((r['window']['hits'], r['window']['hits_dropped']), (70, 70 - r['window']['owner_hits']))
        e = r['evict']                                   # one draw per face: the oldest clocks go first (review F1)
        self.assertEqual((e['patches'], e['cells'], e['cells_dropped'], e['spills']), (5, 128, 36, 0))  # +z culled first
        self.assertLess(e['kept_max'], e['oldest'] - 0.05)
        sp = r['spill']                                  # the default: second draws, nothing dropped, each cell once
        self.assertEqual((sp['patches'], sp['cells'], sp['cells_dropped'], sp['spills'], sp['dupes']), (8, 434, 0, 3, 0))
        self.assertEqual((sp['hits1'], sp['seed1']), (sp['hits0'], 1))  # every draw carries the ring
        self.assertEqual((r['dir_unused']['identical'], r['dir_unused']['cells'] > 0), (1, True))
        for name, sign, seed in (('ytop', 1, 4), ('ybottom', -1, 5)):  # Run236: the y faces under hits from above / below
            y = r[name]
            self.assertEqual((y['patches'], y['seed']), (5, seed), name)  # six faces, +z culled
            self.assertAlmostEqual(y['anchor'], 200.0, places=1)              # 200 along +y / -y
            self.assertEqual((y['hit_u'], y['hit_v']), (0.0, 0.0), name)
        yt = self.ytop_frames()
        self.assertEqual(yt['ytop'], ('0,1,0', '0,0,1', '1,0,0', '0,1,0'))       # a, u, v, view a: (u, v, a) = (z, x, y)
        self.assertEqual(yt['ybottom'], ('0,-1,0', '0,0,1', '-1,0,0', '0,-1,0'))
        ck = r['cockpit']                                # Flight C: only the own ship, only in the cockpit
        self.assertAlmostEqual(ck['own'], 0.35, places=4)
        self.assertAlmostEqual(ck['gain'], 0.35, places=4)
        self.assertEqual((ck['chase'], ck['other'], ck['none'], ck['node']), (1.0, 1.0, 1.0, 1))
        self.assertEqual(int(ck['dimmed']), 5000)     # the own ship's five face draws
        cl = r['clock']                                  # review F2: a stale cockpit visit dims nothing
        self.assertEqual((cl['fresh'], cl['previous'], cl['stale'], cl['revisit'], cl['chase']), (1, 1, 0, 1, 0))

    def test_ship_routing_eye_sphere_and_mapping(self):
        """5.1.1 "Placement" (user decision 2026-11-12 on FL 69 A: the runtime hull walk removed): a ship draws on the
        lathe of its offline profile whatever its tier, a ship without a usable one draws nothing (the hook forwards it
        native), a station keeps the hull decal, the own ship in the cockpit takes the eye sphere of its profile bounds;
        the eye sphere's radial mapping, its sheet box and the axis-plane witness."""
        r = self.run_case('bubble')
        nose, flank, centre = r['nose'], r['flank'], r['centre']
        self.assertAlmostEqual(nose['bz'], 10.0 + 23.0, delta=1e-3)            # the bubble's nose
        self.assertAlmostEqual(nose['by'], -5.0, delta=1e-4)
        self.assertAlmostEqual(flank['bx'], 69.0, delta=1e-3)
        self.assertAlmostEqual(centre['bz'], 33.0, delta=1e-3)                  # a hit at the centre: +z
        t = r['tilted']
        self.assertAlmostEqual(t['on_bubble'], 1.0, delta=1e-5)
        self.assertLess(t['radial'], 1e-5)
        for case in ('fighter_profile', 'capital_profile'):                    # the lathe whatever the tier
            c = r['route_' + case]
            self.assertGreater(c['patches'], 0, case)
            self.assertEqual((c['lathe'], c['eye'], c['decal'], c['lathes']), (c['patches'], 0, 0, 1), case)
            self.assertEqual((c['no_profile'], c['mismatch'], c['unreadable'], c['stale']), (0, 0, 0, 0), case)
        for case, field in (('fighter_none', 'no_profile'), ('capital_none', 'no_profile'), ('mismatch', 'mismatch'),
                            ('own_none', 'no_profile'), ('stale', 'stale'), ('no_table', 'no_profile')):
            c = r['route_' + case]                                              # nothing drawn, counted once
            self.assertEqual((c['patches'], c['lathes'], c['eye_spheres'], c[field]), (0, 0, 0, 1), case)
        st = r['route_station']
        self.assertGreater(st['patches'], 0)
        self.assertEqual((st['decal'], st['lathe'], st['eye'], st['lathes'], st['no_profile']), (st['patches'], 0, 0, 0, 0))
        own = r['route_own_cockpit']            # the profile's 200-unit half-extents x 1.15, not the lathe
        self.assertGreater(own['patches'], 0)
        self.assertEqual((own['eye'], own['eye_spheres'], own['lathe'], own['lathes']), (own['patches'], 1, 0, 0))
        self.assertAlmostEqual(own['eye_r'], 230.0, delta=0.05)
        ap = r['axis_plane']                    # review F2: Q_x = +eps, -eps, 0 light the same cells, none twice
        self.assertEqual((ap['identical'], ap['dupes']), (1, 0))
        self.assertEqual((ap['draws_plus'], ap['seeds_plus']), (ap['draws_minus'], ap['seeds_minus']))
        self.assertGreater(ap['cells'], 0)
        kb = r['keybound']                      # review F6: cells past the key bound counted
        self.assertGreater(kb['key_dropped'], 0)
        d = r['deferred']                       # review F5: two builds a frame, the third the next frame
        self.assertEqual((d['per_frame'], d['first_lathes'], d['first_deferred'], d['live']), (2, 2, 1, 3))
        self.assertEqual((d['second_lathes'], d['second_deferred'], d['builds']), (3, 0, 3))
        ns = r['not_ship']                      # review F4: LatheRefusal::not_ship counted with no_profile
        self.assertEqual((ns['patches'], ns['no_profile']), (0, 1))
        sh = r['sheet']
        self.assertEqual((sh['patches'], sh['seed']), (1, 1))                  # -z only: the rim faces dropped
        self.assertLess(sh['a1'], 100.1)                                       # the front cap of the sphere
        self.assertGreater(sh['a1'], 99.0)
        self.assertGreater(sh['a0'], 100.0 * 0.5)
        self.assertGreaterEqual(sh['u0'], -100.0 * 0.85)

    def test_shield_colour_bands_and_tiers(self):
        out = subprocess.run([str(self.exe), 'stage'], capture_output=True, text=True, check=True).stdout
        line = next(l for l in out.splitlines() if l.startswith('colour '))
        c = {k: tuple(float(x) for x in v.split(',')) for k, v in re.findall(r'(f\d+)=([0-9.,]+)', line)}
        blue, yellow, red = (0.45, 0.72, 1.0), (1.0, 0.88, 0.42), (1.0, 0.4, 0.36)
        self.assertEqual(c['f90'], blue)
        self.assertEqual(c['f75'], blue)                                 # at or above 75 %
        self.assertEqual(c['f50'], yellow)                               # near 50 %
        self.assertEqual(c['f35'], red)                                  # at or below 35 %
        self.assertEqual(c['f20'], red)
        for k, lo, hi in (('f60', yellow, blue), ('f40', red, yellow)):   # smooth in between, inside the pair's box
            for i in range(3):
                self.assertGreaterEqual(c[k][i], min(lo[i], hi[i]) - 1e-4)
                self.assertLessEqual(c[k][i], max(lo[i], hi[i]) + 1e-4)
            self.assertNotEqual(c[k], lo)
            self.assertNotEqual(c[k], hi)
        t = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', next(l for l in out.splitlines() if l.startswith('tiers ')))}
        self.assertEqual((t['m3'], t['m5'], t['m6'], t['ts'], t['m7'], t['m2'], t['tl'], t['unknown']),
                         (0, 0, 1, 1, 2, 2, 2, 255))
        self.assertEqual((t['r10m'], t['r50m'], t['r200m']), (0, 1, 2))

    def test_present_latch_and_ring_scope(self):
        r = self.run_case('latch')
        scope = {k: int(v) for k, v in r['scope'].items()}
        # the first known key and an unknown read keep; a new key, a load and a new epoch clear; unknown keeps the key
        self.assertEqual((scope['first'], scope['unknown'], scope['sector'], scope['same'], scope['loading'],
                          scope['epoch1'], scope['epoch2'], scope['held']), (0, 0, 1, 0, 1, 0, 1, 0x222))
        owned = {k: int(v) for k, v in r['owned'].items()}
        # armed + resolve completed + (callback ran and drew / had nothing) owns; a failed resolve or callback does not
        self.assertEqual(owned, {'drew': 1, 'nothing_to_draw': 1, 'callback_unset': 1, 'failed_resolve': 0,
                                 'callback_failed': 0, 'unarmed': 0})


if __name__ == '__main__':
    unittest.main()
