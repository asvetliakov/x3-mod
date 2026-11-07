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
struct IDirect3DVertexBuffer9; // step 4: the bolt draw's buffers
struct IDirect3DIndexBuffer9;
'''

HARNESS = r'''
#include "weapon_effects_core.h"
#include "weapon_effects_stage.h"
#include "weapon_effects_hull.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace core = x3m::weapon_effects::core;
namespace st = x3m::weapon_effects::stage;
static long I(const char* s) { return std::strtol(s, nullptr, 0); }
// A fake engine memory at 0x100000..0x110000 (32-bit addresses, as the records carry): object k at base + k * 0x400,
// its node at + 0x200. The reader refuses anything outside, as engine_memory::read refuses unreadable memory.
static unsigned char mem[0x40000];
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
// The hull stage into the sprite and glow buffers only (weapon_effects_hull.h HullBuffers).
static st::HullCounts build_hull2(core::Ring& ring, st::HullState& hs, const st::HullView& v, x3m::renderer::WeaponSpriteVertex* spr,
                                  x3m::renderer::WeaponGlowVertex* glw, unsigned* q, unsigned* b) {
    st::HullBuffers o;
    o.sprites = spr;
    o.sprite_cap = 2304;
    o.glows = glw;
    o.glow_cap = 512;
    const st::HullCounts n = st::build_hull(ring, hs, &rd, v, st::HullTuning{}, o);
    *q = o.sprite_quads;
    *b = o.glow_boxes;
    return n;
}
// The energy class's spatter of a record alive at `age` (its one set, the slow one: its count, factors and floor).
static unsigned spatter_alive(const core::Event& e, float k, float age, float floor_ms) {
    const unsigned n = st::spark_count(e, k, st::HullTuning{}, core::hull::an_energy, 1);
    const st::SparkSet ss = st::spark_set(core::hull::an_energy, 1);
    unsigned alive = 0;
    for (unsigned sp = 0; sp < n; ++sp) {
        const float law = (ss.life + ss.life_k * k) * st::spark_factors(e.sequence, sp, ss.speed_spread).life_x;
        alive += age < (law > floor_ms ? law : floor_ms);
    }
    return alive;
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

// The ship bubble (weapon_effects_bubble.h, 5.1.1 "Placement") over the same fake memory: a body cache at base + 0x8000
// (slot, root, hash, one bucket chain, models and LOD records) and hull part nodes at base + 0xa000 (0x270 each).
constexpr std::uint32_t slot = base + 0x8000, croot = base + 0x8010, chash = base + 0x8040, buckets = base + 0x8080;
static std::uint32_t entry(unsigned i) { return base + 0x80c0 + 0x10 * i; }
static std::uint32_t model(unsigned i) { return base + 0x8200 + 0x20 * i; }
static std::uint32_t lods(unsigned i) { return base + 0x8400 + 0x40 * i; }
static std::uint32_t lod(unsigned i, unsigned k) { return base + 0x8800 + 0x80 * i + 0x20 * k; }
static std::uint32_t part(unsigned j) { return base + 0xa000 + 0x270 * j; }
static void put16(std::uint32_t a, std::uint16_t v) { std::memcpy(mem + (a - base), &v, 2); }
// The body cache: one bucket (size 1), the chain of `bodies` entries (ids 7 + i, keys id + 1), model i with `nl` LOD
// records of extents ext[i] (normalised 65536 = 1.0).
static void body_cache(unsigned bodies, const std::int32_t (*ext)[3], unsigned nl = 2) {
    std::memset(mem + (slot - base), 0, 0x2000);
    put(slot, croot);
    put(croot + 0x14, chash);
    put(chash, buckets);
    put(chash + 4, 1);
    put(buckets, entry(0));
    for (unsigned i = 0; i < bodies; ++i) {
        put(entry(i), i + 1 < bodies ? entry(i + 1) : 0);
        put(entry(i) + 4, 7 + i + 1);
        put(entry(i) + 8, model(i));
        put(model(i) + 0x0c, lods(i));
        put16(model(i) + 0x10, std::uint16_t(nl));
        for (unsigned k = 0; k < nl; ++k) {
            put(lods(i) + 4 * k, lod(i, k));
            for (unsigned c = 0; c < 3; ++c) put(lod(i, k) + 0x20 + 4 * c, std::uint32_t(k ? ext[i][c] / 2 : ext[i][c]));
        }
    }
}
// Hull part j: body id (0: none), offset (engine units in the parent frame), node scale s, the basis turned 90 degrees
// about z when `turned`, its next sibling (0: the parent's sentinel) and its first child (0: none).
static void hull_part(unsigned j, std::uint32_t parent, std::int32_t body, const std::int32_t o[3], std::int32_t s,
                      bool turned, std::uint32_t next, std::uint32_t child) {
    std::memset(mem + (part(j) - base), 0, 0x270);
    put(part(j), next ? next : parent + 0x10);
    put(part(j) + 0x0c, child ? child : part(j) + 0x10);
    for (unsigned c = 0; c < 3; ++c) put(part(j) + 0x30 + 4 * c, std::uint32_t(o[c]));
    const std::int32_t rows[3][3] = {{turned ? 0 : 0x10000, turned ? 0x10000 : 0, 0},
                                     {turned ? -0x10000 : 0, turned ? 0 : 0x10000, 0},
                                     {0, 0, 0x10000}};
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c) put(part(j) + 0x40 + 0x10 * r + 4 * c, std::uint32_t(rows[r][c]));
    put(part(j) + 0x70, std::uint32_t(s));
    for (unsigned c = 0; c < 3; ++c) put(part(j) + 0x80 + 4 * c, 0x10000);
    put(part(j) + 0x12c, 0x1000000);
    put(part(j) + 0x140, std::uint32_t(body));
}
// Owner k's root node takes `first` as its first child (its axis scales 1.0).
static void hull_root(unsigned k, std::uint32_t first) {
    put(node(k) + 0x0c, first ? first : node(k) + 0x10);
    put(node(k) + 0x10, 0);
    for (unsigned c = 0; c < 3; ++c) put(node(k) + 0x80 + 4 * c, 0x10000);
}
static void show(const char* name, const st::Bubble& b, bool ok) {
    std::printf("%s ok=%u cx=%.3f cy=%.3f cz=%.3f ex=%.3f ey=%.3f ez=%.3f\n", name, unsigned(ok), b.c[0], b.c[1], b.c[2],
                b.e[0], b.e[1], b.e[2]);
}
// owner_bubble with an ample walk budget (the budget's own case below) and the harness's stats.
static st::BubbleStats bst;
static st::Bubble own_bubble(st::BubbleCache* c, std::uintptr_t cache_root, std::uint32_t owner, std::int32_t radius,
                     std::uint32_t now, bool* fb) {
    unsigned budget = 1000;
    return st::owner_bubble(c, &rd, cache_root, owner, node(0), 0xd000, radius, now, &budget, &bst, fb);
}
static int bubble_cases() {
    static core::Ring ring;
    x3m::renderer::WeaponPatch patches[core::max_patches];
    x3m::renderer::WeaponDecal decals[core::max_decals];
    st::Camera cam;
    cam.r[0] = cam.r[4] = cam.r[8] = 1.f;
    // 1 one hull part: body 7, half-extents (60, 10, 20) view units (6000, 1000, 2000 engine: the extents' largest
    // axis 65536 at node scale 6000), offset (0, -500, 1000) engine: the box about the offset, the bubble its centre
    // and half-extents x bubble_scale; the walk counts one part and one node.
    owner(0, 40000, 20000);
    const std::int32_t ext1[1][3] = {{65536, 65536 / 6, 65536 / 3}};
    body_cache(1, ext1);
    const std::int32_t o1[3] = {0, -500, 1000}, zero[3] = {0, 0, 0};
    hull_part(0, node(0), 7, o1, 6000, false, 0, 0);
    hull_root(0, part(0));
    st::HullBox hb;
    bool ok = st::hull_bounds(&rd, slot, node(0), &hb);
    st::Bubble b;
    bool cut = false;
    ok = ok && st::bubble_from_box(hb, 20000, &b, &cut);
    std::printf("walk ok=%u parts=%u nodes=%u lo=%.0f,%.0f,%.0f hi=%.0f,%.0f,%.0f\n", unsigned(ok), hb.parts, hb.nodes,
                hb.lo[0], hb.lo[1], hb.lo[2], hb.hi[0], hb.hi[1], hb.hi[2]);
    show("one_part", b, ok);
    // 2 two parts and a grandchild below a turned node: A (body 7) aligned at (+3000, 0, 0); B (no body, its basis
    // turned) at (0, 0, -4000) with C (body 8, half-extents 500 each) at (0, 2000, 0) in B's frame: C's position is
    // known only to |(0, 2000, 0)| about B's and its extents to their length (866) on every axis.
    const std::int32_t ext2[2][3] = {{65536, 65536, 65536}, {65536, 65536, 65536}};
    body_cache(2, ext2);
    const std::int32_t oa[3] = {3000, 0, 0}, ob[3] = {0, 0, -4000}, oc[3] = {0, 2000, 0};
    hull_part(0, node(0), 7, oa, 1000, false, part(1), 0);
    hull_part(1, node(0), 0, ob, 1000, true, 0, part(2));
    hull_part(2, part(1), 8, oc, 500, false, 0, 0);
    hull_root(0, part(0));
    ok = st::hull_bounds(&rd, slot, node(0), &hb);
    std::printf("nested ok=%u parts=%u nodes=%u lo=%.0f,%.0f,%.0f hi=%.0f,%.0f,%.0f\n", unsigned(ok), hb.parts, hb.nodes,
                hb.lo[0], hb.lo[1], hb.lo[2], hb.hi[0], hb.hi[1], hb.hi[2]);
    // 3 the guards: 129 sibling parts (past the 128-node budget), an extent past 0x20000, no body cache, a missing
    // model (an unknown body id), a part four levels down (past the depth cap: not visited, the walk still succeeds on
    // the parts above it); each failure gives the fallback sphere through owner_bubble.
    {
        const std::int32_t e1[1][3] = {{65536, 65536, 65536}};
        body_cache(1, e1);
        for (unsigned j = 0; j < 129; ++j) hull_part(j, node(0), 7, zero, 1000, false, j + 1 < 129 ? part(j + 1) : 0, 0);
        hull_root(0, part(0));
        const bool over = st::hull_bounds(&rd, slot, node(0), &hb);
        for (unsigned j = 0; j < 128; ++j) hull_part(j, node(0), 7, zero, 1000, false, j + 1 < 128 ? part(j + 1) : 0, 0);
        const bool at = st::hull_bounds(&rd, slot, node(0), &hb);
        const unsigned at_nodes = hb.nodes;
        hull_part(0, node(0), 7, zero, 1000, false, 0, 0);
        put(lod(0, 0) + 0x20, 0x20001);
        const bool extent = st::hull_bounds(&rd, slot, node(0), &hb);
        body_cache(1, e1);
        const bool no_cache = st::hull_bounds(&rd, base + 0x7ff0 - 0x10000, node(0), &hb);
        hull_part(0, node(0), 99, zero, 1000, false, 0, 0);
        const bool unknown = st::hull_bounds(&rd, slot, node(0), &hb);
        // depth: part 0 (body) -> 1 -> 2 -> 3 (body, four levels down: not visited)
        hull_part(0, node(0), 7, zero, 1000, false, 0, part(1));
        hull_part(1, part(0), 0, zero, 1000, false, 0, part(2));
        hull_part(2, part(1), 0, zero, 1000, false, 0, part(3));
        const std::int32_t far[3] = {0, 0, 900000};
        hull_part(3, part(2), 7, far, 1000, false, 0, 0);
        const bool deep = st::hull_bounds(&rd, slot, node(0), &hb);
        std::printf("guards over_budget=%u at_budget=%u at_nodes=%u extent=%u no_cache=%u unknown_body=%u deep=%u "
                    "deep_nodes=%u deep_hi_z=%.0f\n",
                    unsigned(over), unsigned(at), at_nodes, unsigned(extent), unsigned(no_cache), unsigned(unknown),
                    unsigned(deep), hb.nodes, hb.hi[2]);
        bool fb = false;
        const st::Bubble f = own_bubble(nullptr, base + 0x7ff0 - 0x10000, obj(0), 20000, 0, &fb);
        show("fallback_sphere", f, fb);
    }
    // 4 the cache: one walk, then cached answers; a radius change walks again; a scope change clears; a failed walk is
    // held for bubble_retry_ms of game time (the fallback meanwhile), then retried; 64 entries, the least recently used
    // evicted.
    {
        body_cache(1, ext1);
        hull_part(0, node(0), 7, o1, 6000, false, 0, 0);
        hull_root(0, part(0));
        static st::BubbleCache cache;
        cache.rescope(5);
        bool fb = false;
        auto get = [&](std::int32_t radius, std::uint32_t now) {
            return own_bubble(&cache, slot, obj(0), radius, now, &fb);
        };
        get(20000, 1000);
        const unsigned w1 = cache.walks;
        get(20000, 1016);
        get(20000, 1032);
        const unsigned w2 = cache.walks, h2 = cache.hits;
        get(21000, 1048);
        const unsigned w3 = cache.walks;
        cache.rescope(5);
        get(21000, 1064);
        const unsigned w4 = cache.walks;
        cache.rescope(6);
        get(21000, 1080);
        const unsigned w5 = cache.walks, clears = cache.clears;
        put(part(0) + 0x140, 99); // the model gone: the walk fails
        cache.rescope(7);
        get(21000, 2000);
        const unsigned f1 = cache.failures, w6 = cache.walks;
        const bool fb1 = fb;
        get(21000, 3999);
        const unsigned w7 = cache.walks, held = cache.held;
        put(part(0) + 0x140, 7);
        const st::Bubble back = get(21000, 4000);
        const unsigned w8 = cache.walks;
        std::printf("cache first=%u cached=%u hits=%u radius=%u same_scope=%u scope=%u clears=%u failed=%u walks_failed=%u "
                    "fallback=%u held=%u walks_held=%u retried=%u retried_fallback=%u retried_ez=%.3f\n",
                    w1, w2, h2, w3, w4, w5, clears, f1, w6, unsigned(fb1), held, w7, w8, unsigned(fb), back.e[2]);
        // LRU: 65 owners, the first touched again before the 65th: the second (the least recently used) is evicted
        cache.rescope(8);
        for (unsigned k = 0; k < 64; ++k)
            own_bubble(&cache, slot, 0x1000 + k, 20000, 5000, &fb);
        own_bubble(&cache, slot, 0x1000, 20000, 5000, &fb);
        own_bubble(&cache, slot, 0x2000, 20000, 5000, &fb); // a 65th: evicts 0x1001
        const unsigned before = cache.walks;
        own_bubble(&cache, slot, 0x1000, 20000, 5000, &fb); // still cached
        const unsigned kept = cache.walks - before;
        own_bubble(&cache, slot, 0x1001, 20000, 5000, &fb); // walked again
        std::printf("lru walks_65=%u kept_walks=%u evicted_walks=%u\n", before, kept, cache.walks - before - kept);
    }
    // 5 the mapping on the one-part bubble (centre (0, -5, 10), half-extents 1.15 (60, 10, 20)): the hull point on the
    // hull ellipsoid's nose maps to the bubble's nose (B = c + e Q, on the bubble: |(B - c) / e| = 1, along the radial
    // line of q); the nose lights +z and both faces of x and y, never -z; a flank hit (+x) lights +x and both of y and
    // z; a hit at the centre takes +z.
    {
        body_cache(1, ext1);
        hull_part(0, node(0), 7, o1, 6000, false, 0, 0);
        hull_root(0, part(0));
        bool fb = false;
        const st::Bubble bb = own_bubble(nullptr, slot, obj(0), 20000, 0, &fb);
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
        float r2 = 0.f, cross = 0.f;
        float q0[3];
        for (unsigned i = 0; i < 3; ++i) {
            const float qq = (B[i] - bb.c[i]) / bb.e[i];
            r2 += qq * qq;
            q0[i] = (tilted[i] - bb.c[i]) / bb.e[i];
        }
        // the radial line: (B - c) / e parallel to q (their cross product's length)
        const float bq[3] = {(B[0] - bb.c[0]) / bb.e[0], (B[1] - bb.c[1]) / bb.e[1], (B[2] - bb.c[2]) / bb.e[2]};
        const float cx = bq[1] * q0[2] - bq[2] * q0[1], cy = bq[2] * q0[0] - bq[0] * q0[2], cz = bq[0] * q0[1] - bq[1] * q0[0];
        cross = std::sqrt(cx * cx + cy * cy + cz * cz);
        std::printf("tilted on_bubble=%.6f radial=%.6f\n", r2, cross);
    }
    // 6 through the builder: a nose hit on the one-part ship (cached walk): its draws are bubble draws on its faces
    // with the nose's projection inside each box, none on -z; the station tier keeps the hull decal; the fallback
    // counted when the walk fails.
    {
        core::ring_clear(ring);
        owner(0, 40000, 20000);
        body_cache(1, ext1);
        hull_part(0, node(0), 7, o1, 6000, false, 0, 0);
        hull_root(0, part(0));
        core::Event* e = hit(ring, 0, 1000, 0, 0xffff, 3000); // the hull's nose (z 30 view units)
        e->pos[1] = -500;
        e->tier_source = core::tier_by_ship_class;
        st::Tuning t{};
        t.body_cache_root = slot;
        static st::BubbleCache cache;
        cache.rescope(1);
        st::Counts n = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals, &cache);
        unsigned bubble = 0, minus_z = 0, inside = 0, seeds = 0;
        const float bn[3] = {0.f, -5.f, 10.f + 1.15f * 20.f}; // the bubble's nose
        for (unsigned i = 0; i < n.patches; ++i) {
            const auto& q = patches[i];
            bubble += q.bubble;
            minus_z += q.seed == 1.f;
            seeds |= 1u << unsigned(q.seed);
            const float f[3] = {st::dot3(bn, q.anchor_u), st::dot3(bn, q.anchor_v), st::dot3(bn, q.anchor)};
            bool in = q.seed == 0.f; // the +z face's box holds the bubble's nose
            for (unsigned k = 0; k < 3; ++k) in = in && f[k] >= q.box[k] - 1e-3f && f[k] <= q.box[3 + k] + 1e-3f;
            inside += in;
        }
        std::printf("build patches=%u bubble=%u minus_z=%u inside=%u seeds=%u bubbles=%u fallback=%u walks=%u ez=%.3f\n",
                    n.patches, bubble, minus_z, inside, seeds, n.bubbles, n.bubble_fallback, cache.walks,
                    n.patches ? patches[0].bubble_e[2] : 0.f);
        n = st::build(ring, 1516, &rd, cam, t, patches, core::max_patches, decals, core::max_decals, &cache);
        std::printf("rebuild walks=%u hits=%u fallback=%u\n", cache.walks, cache.hits, n.bubble_fallback);
        e->tier_source = core::tier_by_station;
        n = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals, &cache);
        unsigned decal = 0;
        for (unsigned i = 0; i < n.patches; ++i) decal += !patches[i].bubble;
        std::printf("station patches=%u decal=%u bubbles=%u\n", n.patches, decal, n.bubbles);
        e->tier_source = core::tier_by_radius;
        t.body_cache_root = 0x10;
        n = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals);
        std::printf("fallback patches=%u bubbles=%u bubble_fallback=%u ex=%.3f\n", n.patches, n.bubbles, n.bubble_fallback,
                    n.patches ? patches[0].bubble_e[0] : 0.f);
    }
    // 7 the box over the bubble's sheet: a unit-ish sphere bubble (radius 100) with one hit on its front lights -z and
    // both faces of x and y; the x and y draws' cells lie on the rim of their sheet (steeper than the cut): dropped;
    // the -z draw's box spans the sheet's depths over its cells (within the sphere's front cap).
    {
        core::ring_clear(ring);
        owner(0, 40000, 10000);
        core::Event* e = hit(ring, 0, 1000, 0, 0xffff, -10000);
        e->tier_source = core::tier_by_radius;
        st::Tuning t{};
        t.body_cache_root = 0x10; // the fallback sphere: radius 100 view units
        const st::Counts n = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals);
        std::printf("sheet patches=%u seed=%.0f a0=%.3f a1=%.3f u0=%.3f u1=%.3f fallback=%u\n", n.patches,
                    n.patches ? patches[0].seed : -1.f, n.patches ? patches[0].box[2] : 0.f,
                    n.patches ? patches[0].box[5] : 0.f, n.patches ? patches[0].box[0] : 0.f,
                    n.patches ? patches[0].box[3] : 0.f, n.bubble_fallback);
    }
    // 8 a top hit on the one-part ship a hair either side of the x axis plane (review F2: Q_x = +eps, -eps and 0): every
    // face lit by all three, the same draws with the same cells and clocks (no flip of the lit faces, no cell twice).
    {
        auto draws_of = [&](std::int32_t x, double* keys, unsigned* nkeys) {
            core::ring_clear(ring);
            owner(0, 40000, 20000);
            body_cache(1, ext1);
            hull_part(0, node(0), 7, o1, 6000, false, 0, 0);
            hull_root(0, part(0));
            core::Event* e = hit(ring, 0, 1000, x, 0xffff, 1000);
            e->pos[1] = 500; // the hull's top (centre y -5 view units + 10)
            e->tier_source = core::tier_by_ship_class;
            st::Tuning t{};
            t.body_cache_root = slot;
            const st::Counts n = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals);
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
        const auto dp = draws_of(1, kp, &np), dm = draws_of(-1, km, &nm), d0 = draws_of(0, k0, &n0);
        bool same = np == nm && np == n0;
        for (unsigned i = 0; same && i < np; ++i) same = kp[i] == km[i] && kp[i] == k0[i];
        unsigned dupes = 0;
        for (unsigned i = 1; i < np; ++i) dupes += kp[i] == kp[i - 1];
        std::printf("axis_plane draws_plus=%u draws_minus=%u draws_zero=%u seeds_plus=%u seeds_minus=%u cells=%u "
                    "identical=%u dupes=%u\n", dp.first, dm.first, d0.first, dp.second, dm.second, np, unsigned(same), dupes);
    }
    // 9 the walk budget (review F4): three ships whose bounds are not cached in one frame: two walk, the third draws on the
    // fallback sphere (deferred) and walks on the next frame; then a changed first child (another hull) walks again
    // (review F7) and a bubble wider than the object radius is cut to it (review F5).
    {
        core::ring_clear(ring);
        body_cache(1, ext1);
        hull_part(0, node(0), 7, o1, 6000, false, 0, 0);
        for (unsigned k = 0; k < 3; ++k) {
            owner(k, 40000, 20000);
            hull_root(k, part(0));
            core::Event* e = hit(ring, k, 1000, 0, 0xffff, 3000);
            e->tier_source = core::tier_by_ship_class;
        }
        st::Tuning t{};
        t.body_cache_root = slot;
        static st::BubbleCache cache;
        cache.rescope(1);
        const st::Counts a = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals, &cache);
        const st::Counts b2 = st::build(ring, 1516, &rd, cam, t, patches, core::max_patches, decals, core::max_decals, &cache);
        const st::Counts c3 = st::build(ring, 1532, &rd, cam, t, patches, core::max_patches, decals, core::max_decals, &cache);
        std::printf("budget walks=%u deferred=%u fallback=%u next_walks=%u next_deferred=%u next_fallback=%u "
                    "then_walks=%u then_hits=%u per_frame=%u\n", a.bubble.walks, a.bubble.deferred, a.bubble_fallback,
                    b2.bubble.walks, b2.bubble.deferred, b2.bubble_fallback, c3.bubble.walks, c3.bubble.deferred,
                    core::bubble_walks_per_frame);
        // the first child of owner 0 becomes another part (half the extents): walked again, a narrower bubble
        const std::int32_t half_ext[1][3] = {{65536, 65536 / 6, 65536 / 3}};
        (void)half_ext;
        hull_part(1, node(0), 7, o1, 3000, false, 0, 0);
        hull_root(0, part(1));
        const st::Counts d = st::build(ring, 1548, &rd, cam, t, patches, core::max_patches, decals, core::max_decals, &cache);
        float ex = 0.f;
        for (unsigned i = 0; i < d.patches; ++i)
            if (patches[i].owner == obj(0)) ex = patches[i].bubble_e[0];
        std::printf("child walks=%u ex=%.3f\n", d.bubble.walks, ex);
        // the object radius 20 view units (2000 engine) under the 69 x 11.5 x 23 bubble: x cut to 1.15 x 20 = 23
        core::ring_clear(ring);
        owner(0, 40000, 2000);
        hull_root(0, part(0));
        core::Event* e = hit(ring, 0, 1000, 0, 0xffff, 2000);
        e->tier_source = core::tier_by_ship_class;
        const st::Counts c = st::build(ring, 1500, &rd, cam, t, patches, core::max_patches, decals, core::max_decals);
        std::printf("clamp clamped=%u ex=%.3f ey=%.3f ez=%.3f\n", c.bubble.clamped, c.patches ? patches[0].bubble_e[0] : 0.f,
                    c.patches ? patches[0].bubble_e[1] : 0.f, c.patches ? patches[0].bubble_e[2] : 0.f);
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
        // the side planes (3) or past the far plane (4); the hull stage adds its kept owners in view (hull::max_owners of
        // 40, the newest hits) and the stamp marks the 8 others' records hidden.
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
        st::build(ring, 1010, &rd, cam, st::Tuning{}, patches, core::max_patches, decals, core::max_decals, nullptr,
                  &shield_drawn);
        core::ring_stamp_hidden(ring, shield_drawn);
        unsigned shield_hidden = 0;
        for (unsigned i = 0; i < core::ring_capacity; ++i)
            shield_hidden += ring.slots[i].sequence && (ring.hidden[i] & core::hidden_undrawn);
        const bool shield_ok = shield_drawn.count == 1 && shield_drawn.owner[0] == obj(1) && shield_hidden == 3;
        core::ring_clear(ring);
        for (unsigned k = 1; k <= 40; ++k) {
            owner(k, 40000, 10000);
            core::Event* e = core::ring_acquire(ring);
            const std::uint32_t seq = e->sequence;
            std::memset(e, 0, sizeof *e);
            e->sequence = seq;
            e->role = core::role_hull_hit;
            e->flags = core::flag_stage;
            e->table = core::table_bullets;
            e->t0_game_ms = 1000 + k;
            e->lifetime_ms = core::hull_lifetime_ms;
            e->damage = 3000;
            e->owner = obj(k);
            e->node = node(k);
            e->handle = 0xd000 + k;
            e->pos[2] = -10000;
            e->radius = 10000;
        }
        static st::HullState hs;
        st::hull_clear(hs);
        static x3m::renderer::WeaponSpriteVertex spr[x3m::renderer::weapon_sprite_quads * 4];
        static x3m::renderer::WeaponGlowVertex glw[x3m::renderer::weapon_glow_boxes * 4];
        st::HullView v;
        v.cam = cam;
        v.width = 1920;
        v.height = 1080;
        v.near_z = 6;
        v.step_ms = 16;
        v.now = 1100;
        st::HullBuffers o;
        o.sprites = spr;
        o.sprite_cap = 2304;
        o.glows = glw;
        o.glow_cap = 512;
        core::VisibleOwners hull_drawn;
        st::build_hull(ring, hs, &rd, v, st::HullTuning{}, o, &hull_drawn);
        core::ring_stamp_hidden(ring, hull_drawn);
        unsigned hull_hidden = 0, hidden_old = 0;
        for (unsigned i = 0; i < core::ring_capacity; ++i)
            if (ring.slots[i].sequence && (ring.hidden[i] & core::hidden_undrawn)) {
                ++hull_hidden;
                hidden_old += ring.slots[i].t0_game_ms <= 1008;
            }
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
                      nullptr, &vf);
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
        st::build(ring, 1042, &rd, cam, st::Tuning{}, patches, core::max_patches, decals, core::max_decals, nullptr, &vf);
        core::ring_stamp_hidden(ring, vf);
        unsigned bad_off = 0, others_in_view = 0;
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            if (!ring.slots[i].sequence || ring.slots[i].owner > obj(20)) continue;
            if (ring.slots[i].owner == obj(dropped_k)) bad_off += (ring.hidden[i] & core::hidden_off_screen) != 0;
            else others_in_view += !(ring.hidden[i] & core::hidden_off_screen);
        }
        std::printf("hidden=%u census_hidden=%u census_drawn=%u staged_hidden=%u staged_drawn=%u order_ok=%u "
                    "fresh_kept=%u oldest_gone=%u overflow=%u shield_drawn_ok=%u hull_drawn=%u hull_hidden=%u "
                    "hull_hidden_oldest=%u vf_frame1_drawn=%u vf_frame2_drawn=%u vf_on_screen=%u "
                    "vf_dropped_in_view=%u vf_bad_handle_off=%u vf_others_in_view=%u\n",
                    hidden, per[0], per[1], per[2], per[3], order_ok, unsigned(fresh_kept), unsigned(oldest_gone),
                    overflowed, unsigned(shield_ok), hull_drawn.count, hull_hidden, hidden_old, vf_drawn[0], vf_drawn[1],
                    vf_on_screen, dropped_in_view, bad_off, others_in_view);
        return 0;
    }
    if (!std::strcmp(argv[1], "stage")) return stage_cases();
    if (!std::strcmp(argv[1], "owners")) {
        // Review 3: 40 owners with one stage-owned hull hit each (owner k's at t0 1000 + k: the higher k the newer). Past
        // hull::max_owners the oldest newest hits are not drawn: owners 1..8, the same set on the next frame (no
        // blinking); a new hit on owner 1 then brings it in and drops owner 9 (the oldest of those kept).
        static core::Ring ring;
        core::ring_clear(ring);
        for (unsigned k = 1; k <= 40; ++k) {
            owner(k, 40000, 10000);
            core::Event* e = core::ring_acquire(ring);
            const std::uint32_t seq = e->sequence;
            std::memset(e, 0, sizeof *e);
            e->sequence = seq;
            e->role = core::role_hull_hit;
            e->flags = core::flag_stage;
            e->table = core::table_bullets;
            e->t0_game_ms = 1000 + k;
            e->lifetime_ms = core::hull_lifetime_ms;
            e->damage = 3000;
            e->owner = obj(k);
            e->node = node(k);
            e->handle = 0xd000 + k;
            e->pos[2] = -10000;
            e->radius = 10000;
        }
        static st::HullState hs;
        st::hull_clear(hs);
        static x3m::renderer::WeaponSpriteVertex spr[x3m::renderer::weapon_sprite_quads * 4];
        static x3m::renderer::WeaponGlowVertex glw[x3m::renderer::weapon_glow_boxes * 4];
        st::HullView v;
        v.cam.r[0] = v.cam.r[4] = v.cam.r[8] = 1.f;
        v.cam.m00 = 1.6f;
        v.cam.m11 = 1.6f * 16.f / 9.f;
        v.width = 1920;
        v.height = 1080;
        v.near_z = 6;
        v.step_ms = 16;
        auto drawn = [&](unsigned k) {
            for (unsigned i = 0; i < core::ring_capacity; ++i)
                if (ring.slots[i].sequence && ring.slots[i].owner == obj(k)) return hs.slots[i].latched != 0;
            return false;
        };
        auto frame = [&](std::uint32_t now, const char* name) {
            v.now = now;
            unsigned q = 0, b = 0;
            const st::HullCounts n = build_hull2(ring, hs, v, spr, glw, &q, &b);
            unsigned mask_lo = 0, mask_hi = 0;
            for (unsigned k = 1; k <= 40; ++k)
                if (drawn(k)) (k <= 20 ? mask_lo : mask_hi) |= 1u << ((k - 1) % 20);
            std::printf("%s hits=%u owners_dropped=%u lo=%u hi=%u d1=%u d8=%u d9=%u d40=%u\n", name, n.hits, n.owners_dropped,
                        mask_lo, mask_hi, unsigned(drawn(1)), unsigned(drawn(8)), unsigned(drawn(9)), unsigned(drawn(40)));
        };
        frame(1100, "first");
        frame(1116, "second");
        core::Event* e = core::ring_acquire(ring);
        const std::uint32_t seq = e->sequence;
        *e = ring.slots[0];
        e->sequence = seq;
        e->t0_game_ms = 1130;
        e->owner = obj(1);
        e->node = node(1);
        e->handle = 0xd001;
        frame(1132, "third");
        return 0;
    }
    if (!std::strcmp(argv[1], "hull")) {
        // Step 2 (docs/architecture/weapon-look.md 3.1): one stage-owned hull hit on owner 1 (k 0.5, a fighter): its
        // first frame latches the contact point; 100 game ms later its flash is gone (40 ms, the 48 ms floor < 100,
        // 48), its 16 sparks, glow (150 ms) and ember live; a shield hit and an unstaged hull hit draw nothing here; once
        // the owner dies the sparks stay (orphan) and the glow, ember and flash go; the mode words.
        owner(1, 40000, 10000);
        static core::Ring ring;
        core::ring_clear(ring);
        core::Event* e = core::ring_acquire(ring);
        const std::uint32_t seq = e->sequence;
        std::memset(e, 0, sizeof *e);
        e->sequence = seq;
        e->role = core::role_hull_hit;
        e->flags = core::flag_stage;
        e->table = core::table_bullets;
        e->tint_rgb = 0xffffff | core::rgb_present;
        e->t0_game_ms = 1000;
        e->lifetime_ms = core::hull_lifetime_ms;
        e->damage = 3000;
        e->owner = obj(1);
        e->node = node(1);
        e->handle = 0xd001;
        e->pos[2] = -10000;
        e->radius = 10000;
        core::Event* sh = core::ring_acquire(ring);
        *sh = *e;
        sh->sequence = seq + 1;
        sh->role = core::role_shield_hit;
        core::Event* un = core::ring_acquire(ring);
        *un = *e;
        un->sequence = seq + 2;
        un->flags = 0;
        static st::HullState hs;
        st::hull_clear(hs);
        static x3m::renderer::WeaponSpriteVertex spr[x3m::renderer::weapon_sprite_quads * 4];
        static x3m::renderer::WeaponGlowVertex glw[x3m::renderer::weapon_glow_boxes * 4];
        st::HullView v;
        v.cam.r[0] = v.cam.r[4] = v.cam.r[8] = 1.f;
        v.cam.m00 = 1.6f;
        v.cam.m11 = 1.6f * 16.f / 9.f;
        v.width = 1920;
        v.height = 1080;
        v.near_z = 6;
        v.now = 1000;
        v.step_ms = 16;
        unsigned q = 0, b = 0;
        const st::HullCounts first = build_hull2(ring, hs, v, spr, glw, &q, &b);
        const float k0 = st::hull_calibre(ring.slots[0]);
        const unsigned want = st::spark_count(ring.slots[0], k0, st::HullTuning{}, core::hull::an_energy, 1);
        const unsigned alive100 = spatter_alive(ring.slots[0], k0, 100.f, 48.f), alive110 = spatter_alive(ring.slots[0], k0, 110.f, 48.f);
        std::printf("first hits=%u flashes=%u sparks=%u glows=%u embers=%u quads=%u boxes=%u want=%u alive100=%u alive110=%u\n",
                    first.hits, first.flashes, first.sparks, first.glows, first.embers, q, b, want, alive100, alive110);
        v.now = 1100;
        const st::HullCounts later = build_hull2(ring, hs, v, spr, glw, &q, &b);
        std::printf("later hits=%u flashes=%u sparks=%u glows=%u embers=%u floored=%u\n", later.hits, later.flashes,
                    later.sparks, later.glows, later.embers, later.floored);
        put(obj(1) + core::layout::obj_flags, core::layout::obj_dying);
        v.now = 1110;
        const st::HullCounts dead = build_hull2(ring, hs, v, spr, glw, &q, &b);
        std::printf("dead orphans=%u sparks=%u flashes=%u glows=%u embers=%u retired=%u\n", dead.orphans, dead.sparks,
                    dead.flashes, dead.glows, dead.embers, dead.retired);
        core::Mode m = core::mode_native;
        std::printf("mode parsed=%u value=%u hull_word=%u lifetime=%u\n", unsigned(core::parse_mode("shields,hull", 12, &m)),
                    unsigned(m), unsigned(core::parse_mode("hull", 4, &m)), core::hull_lifetime_ms);
        return 0;
    }
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
        # volume (not behind, beside or past the far plane), the hull stage its kept owners (hull::max_owners of 40).
        out = subprocess.run([str(self.exe), 'evict'], capture_output=True, text=True, check=True).stdout
        f = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}
        self.assertEqual(f['hidden'], 512)
        self.assertEqual((f['census_hidden'], f['census_drawn'], f['staged_hidden'], f['staged_drawn']), (256,) * 4)
        self.assertEqual(f['order_ok'], 1)
        self.assertEqual((f['fresh_kept'], f['oldest_gone'], f['overflow']), (1, 1, 1))
        self.assertEqual(f['shield_drawn_ok'], 1)
        self.assertEqual((f['hull_drawn'], f['hull_hidden'], f['hull_hidden_oldest']), (32, 8, 8))
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

    def test_ship_bubble_walk_cache_mapping_and_fallback(self):
        """5.1.1 "Placement" (user decision after Run238 Flight E): the hull bounds walk of render-node-bounds.md 6.1,
        its per-owner cache (scope and radius invalidation, the 2 s retry, LRU), the hit's mapping onto the bubble and
        its faces, the station tier's hull decal and the fallback sphere."""
        r = self.run_case('bubble')
        w = r['walk']
        self.assertEqual((w['ok'], w['parts'], w['nodes']), (1, 1, 1))
        self.assertEqual((w['lo'], w['hi']), (-6000.0, 6000.0))                 # x: the part's +-6000 about 0
        n = r['nested']                         # A aligned, C below the turned B: its slack and |e| on every axis
        self.assertEqual((n['ok'], n['parts'], n['nodes'], n['lo'], n['hi']), (1, 2, 3, -2866.0, 4000.0))
        one = r['one_part']                       # c = the box's centre, e = its half-extents x 1.15 (view units)
        self.assertEqual(one['ok'], 1)
        for k, want in (('cx', 0.0), ('cy', -5.0), ('cz', 10.0)):
            self.assertAlmostEqual(one[k], want, delta=1e-3, msg=k)
        for k, want in (('ex', 60.0), ('ey', 10.0), ('ez', 20.0)):
            self.assertAlmostEqual(one[k], 1.15 * want, delta=0.02, msg=k)
        g = r['guards']
        self.assertEqual((g['over_budget'], g['at_budget'], g['at_nodes']), (0, 1, 128))    # the 128-node cap
        self.assertEqual((g['extent'], g['no_cache'], g['unknown_body']), (0, 0, 0))
        self.assertEqual((g['deep'], g['deep_nodes']), (1, 3))                              # three levels, no deeper
        self.assertLess(g['deep_hi_z'], 2000.0)
        fb = r['fallback_sphere']                                                           # the object's radius
        self.assertEqual(fb['ok'], 1)
        self.assertEqual((fb['cx'], fb['cy'], fb['cz'], fb['ex'], fb['ey'], fb['ez']), (0, 0, 0, 200, 200, 200))
        c = r['cache']
        self.assertEqual((c['first'], c['cached'], c['hits']), (1, 1, 2))      # one walk, then the cache
        self.assertEqual((c['radius'], c['same_scope'], c['scope']), (2, 2, 3))  # a radius change, a scope change
        self.assertEqual((c['failed'], c['walks_failed'], c['fallback']), (1, 4, 1))
        self.assertEqual((c['held'], c['walks_held']), (1, 4))                 # held within 2 s of game time
        self.assertEqual((c['retried'], c['retried_fallback']), (5, 0))        # retried at 2 s, walked
        self.assertAlmostEqual(c['retried_ez'], 23.0, delta=0.02)
        lru = r['lru']
        self.assertEqual((lru['kept_walks'], lru['evicted_walks']), (0, 1))
        nose, flank, centre = r['nose'], r['flank'], r['centre']
        self.assertAlmostEqual(nose['bz'], 10.0 + 23.0, delta=1e-3)            # the bubble's nose
        self.assertAlmostEqual(nose['by'], -5.0, delta=1e-4)
        self.assertAlmostEqual(flank['bx'], 69.0, delta=1e-3)
        self.assertAlmostEqual(centre['bz'], 33.0, delta=1e-3)                  # a hit at the centre: +z
        t = r['tilted']
        self.assertAlmostEqual(t['on_bubble'], 1.0, delta=1e-5)
        self.assertLess(t['radial'], 1e-5)
        b = r['build']
        # +z holds the nose; every face is lit (review F1), -z's draw only where its sheet lies within the nose cells'
        # depth window (this bubble's z half-extent, 23, is under the extent, 32: its rim survives)
        self.assertEqual((b['bubble'], b['inside']), (b['patches'], 1))
        self.assertGreaterEqual(b['patches'], 1)
        self.assertEqual((b['bubbles'], b['fallback'], b['walks']), (1, 0, 1))
        self.assertAlmostEqual(b['ez'], 23.0, delta=0.02)
        self.assertEqual((r['rebuild']['walks'], r['rebuild']['fallback']), (1, 0))  # the second frame: cached
        st = r['station']
        self.assertEqual((st['decal'], st['bubbles']), (st['patches'], 0))
        self.assertGreater(st['patches'], 0)
        f = r['fallback']
        self.assertEqual((f['bubbles'], f['bubble_fallback'], f['ex']), (1, 1, 200.0))
        ap = r['axis_plane']                    # review F2: Q_x = +eps, -eps, 0 light the same cells, none twice
        self.assertEqual((ap['identical'], ap['dupes']), (1, 0))
        self.assertEqual((ap['draws_plus'], ap['seeds_plus']), (ap['draws_minus'], ap['seeds_minus']))
        self.assertGreater(ap['cells'], 0)
        bu = r['budget']                        # review F4: two walks a frame, the third deferred to the next
        self.assertEqual((bu['per_frame'], bu['walks'], bu['deferred'], bu['fallback']), (2, 2, 1, 1))
        self.assertEqual((bu['next_walks'], bu['next_deferred'], bu['next_fallback']), (1, 0, 0))
        self.assertEqual(bu['then_walks'], 0)
        ch = r['child']                         # review F7: a new first child walks again
        self.assertEqual(ch['walks'], 1)
        self.assertAlmostEqual(ch['ex'], 0.5 * 69.0, delta=0.02)
        cl = r['clamp']                         # review F5: the half-extents cut to bubble_scale x the object radius
        self.assertEqual(cl['clamped'], 1)
        self.assertAlmostEqual(cl['ex'], 23.0, delta=1e-3)   # cut to bubble_scale x the radius
        self.assertAlmostEqual(cl['ez'], 23.0, delta=1e-3)   # 1.15 x 20 m of hull: at the bound, not cut
        self.assertAlmostEqual(cl['ey'], 11.5, delta=0.01)
        sh = r['sheet']
        self.assertEqual((sh['patches'], sh['seed'], sh['fallback']), (1, 1, 1))  # -z only: the rim faces dropped
        self.assertLess(sh['a1'], 100.1)                                           # the front cap of the sphere
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

    def test_hull_owner_cap_drops_the_oldest_stably(self):
        r = self.run_case('owners')
        first, second, third = r['first'], r['second'], r['third']
        self.assertEqual((first['hits'], first['owners_dropped']), (32, 8))
        self.assertEqual((first['d1'], first['d8'], first['d9'], first['d40']), (0, 0, 1, 1))
        self.assertEqual((second['lo'], second['hi'], second['owners_dropped']), (first['lo'], first['hi'], 8))  # stable
        self.assertEqual(int(first['lo']), sum(1 << (k - 1) for k in range(9, 21)))
        # owner 1's new hit brings it in; owner 9's old one goes (its record stays latched, not drawn this frame)
        self.assertEqual((third['d1'], third['owners_dropped']), (1, 8))
        self.assertEqual(third['hits'], 33)                       # owner 1's two records, 31 other owners

    def test_hull_build_first_frame_later_and_orphan(self):
        r = self.run_case('hull')
        first, later, dead, mode = r['first'], r['later'], r['dead'], r['mode']
        # the energy class (the nibble 0, hull-impact-classes.md 4.1): its spatter round((4 + 4 k) x (1 +- 0.4)), hashed
        # from the sequence (spark_count), living (150 + 100 k) ms x its factor
        n = int(first['want'])
        self.assertTrue(3 <= n <= 9, n)
        a100, a110 = int(first['alive100']), int(first['alive110'])
        # the glow is the burst's light (hull-burst-look.md 3): R(0) = 0 at the first frame, lit 100 ms later
        self.assertEqual((first['hits'], first['flashes'], first['sparks'], first['glows'], first['embers']), (1, 1, n, 0, 1))
        self.assertEqual((first['quads'], first['boxes']), (n + 1, 1))  # the shield and the unstaged records drew nothing
        self.assertEqual((later['flashes'], later['sparks'], later['glows'], later['embers']), (0, a100, 1, 1))
        self.assertEqual((dead['orphans'], dead['sparks'], dead['flashes'], dead['glows'], dead['embers'], dead['retired']),
                         (1, a110, 0, 0, 0, 0))
        self.assertEqual((mode['parsed'], mode['value'], mode['hull_word'], mode['lifetime']), (1, 3, 0, 3601))

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


# Step 4, review 2026-11-07 finding 1: the previous frame's index (src/proxy/weapon_effects_bolt_stage.h BoltPrevIndex)
# stamped per build. Two build_bolts calls on consecutive frames (400 bolts; 8 new at the front, 8 expired), each
# bolt's previous record must be its own (object and creation) or none for a new one. The stamp the production glue
# passes (the hooks' Present generation, one per frame) against the stamp it replaced (frame | 1, equal on frames 2k and
# 2k + 1: the reviewer's model verification/results/bolt-muzzle-look/prev_index_stamp.py printed 5 -> 24..45 probes and
# wrong records on odd frames); a second build in the same frame (the same stamp) keeps the index.
BOLT_HARNESS = r"""
#include "weapon_effects_bolt_stage.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
namespace core = x3m::weapon_effects::core;
namespace st = x3m::weapon_effects::stage;
static st::BoltScratch scratch;
static x3m::renderer::WeaponBoltQuad quads[x3m::renderer::weapon_bolt_quads];
static st::BoltPrevIndex index_;
static std::vector<core::BoltRecord> frame_list(unsigned frame) {
    // bolts 1000 + i of creation 7000 + i; each frame the 8 oldest expire and 8 new ones lead the list (the tap's order)
    std::vector<core::BoltRecord> v;
    for (unsigned k = 0; k < 8; ++k) {
        core::BoltRecord r{};
        r.obj = 0x9000000u + 16u * frame + k;
        r.created = 5000u + frame;
        v.push_back(r);
    }
    for (unsigned i = 8 * (frame + 1); i < 8 * (frame + 1) + 392; ++i) {
        core::BoltRecord r{};
        r.obj = 0x1000000u + 16u * i;
        r.created = 7000u + i;
        v.push_back(r);
    }
    for (unsigned i = 0; i < v.size(); ++i) {
        v[i].pos[0] = std::int32_t((i % 20) * 500) - 5000;
        v[i].pos[1] = std::int32_t((i / 20) * 300) - 3000;
        v[i].pos[2] = 200000;
        v[i].vel[0] = 65536;
        v[i].expiry = v[i].created + 100000u;
        v[i].row = 0;
    }
    return v;
}
int main(int argc, char** argv) {
    const bool old_stamp = argc > 1 && !std::strcmp(argv[1], "frame_or_1");
    const float tint[3] = {1.f, 0.5f, 0.2f};
    const st::BoltLook look = st::bolt_look_from(0, core::shape_tracer, 0.3f, 0, 2000.f, tint);
    st::BoltView v;
    for (unsigned i = 0; i < 9; ++i) v.cam.r[i] = (i % 4 == 0) ? 1.f : 0.f;
    v.cam.m11 = 1.3333333f;
    v.cam.m00 = 0.75f;
    v.width = 1920.f;
    v.height = 1080.f;
    v.near_z = 5.f;
    v.now = 6000u;
    v.step_ms = 16u;
    unsigned wrong = 0, lost = 0, checked = 0, generation = 40;
    for (unsigned frame = 4; frame < 8; ++frame, ++generation) {
        const std::vector<core::BoltRecord> prev = frame_list(frame - 1), cur = frame_list(frame);
        const std::uint32_t stamp = old_stamp ? (frame | 1u) : generation;
        st::BoltFrameOut out;
        out.quads = quads;
        for (unsigned again = 0; again < 2; ++again) // the second: a second build in the same frame (the same stamp)
            st::build_bolts(v, cur.data(), unsigned(cur.size()), prev.data(), unsigned(prev.size()), index_, stamp, nullptr,
                            [&](unsigned) -> const st::BoltLook& { return look; }, scratch, out);
        for (const core::BoltRecord& r : cur) {
            const int i = st::bolt_prev_find(index_, r.obj, r.created);
            bool listed = false;
            for (const core::BoltRecord& p : prev) listed = listed || (p.obj == r.obj && p.created == r.created);
            ++checked;
            if (i >= 0 && (prev[unsigned(i)].obj != r.obj || prev[unsigned(i)].created != r.created)) ++wrong;
            if (i < 0 && listed) ++lost;
        }
    }
    std::printf("checked=%u wrong=%u lost=%u\n", checked, wrong, lost);
    return 0;
}
"""


class WeaponBoltPrevIndex(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('a host C++ compiler is required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-weapon-bolt-')
        src = Path(cls.tmp.name) / 'b.cpp'
        src.write_text(BOLT_HARNESS)
        stub = Path(cls.tmp.name) / 'stub'
        stub.mkdir()
        (stub / 'd3d9.h').write_text(D3D9_STUB)
        cls.exe = Path(cls.tmp.name) / 'b'
        subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(stub), '-I', str(ROOT / 'src/proxy'), str(src), '-o',
                        str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_harness(self, *args):
        out = subprocess.run([str(self.exe), *args], capture_output=True, text=True, check=True).stdout
        return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}

    def test_previous_record_is_its_own_on_consecutive_frames(self):
        r = self.run_harness()
        self.assertEqual(r['checked'], 4 * 400)
        self.assertEqual((r['wrong'], r['lost']), (0, 0))

    def test_the_replaced_frame_or_1_stamp_hands_out_other_records(self):
        # the negative control: the old stamp repeats on frames 4/5 and 6/7, so odd frames read the even frame's slots
        r = self.run_harness('frame_or_1')
        self.assertGreater(r['wrong'] + r['lost'], 0)


if __name__ == '__main__':
    unittest.main()
