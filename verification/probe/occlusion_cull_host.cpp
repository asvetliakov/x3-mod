// Host driver of the occlusion cull's pure core (src/proxy/occlusion_cull_core.h), compiled with the host compiler by
// verification/analysis/test_occlusion_cull.py: the body-name classification, the test rectangle (inflation, nearest
// depth, refusals), the stability guard, the ring's bookkeeping (pool, previous slot, lookups across a frame gap and a
// clear), the skip rule, the draw key, the classifier over a synthetic engine image (hull owners, the part walk,
// the per-node memo, ship roots), the reprojection through the hull rows and the per-ship batching (blocks, cadence,
// hull signature, movement, reset, stale rectangles), and the engine-side skip's core (src/proxy/occlusion_engine_core.h:
// the mode parser, the position window, the per-node ledger, the verdict table's publish rule and the stub's logic
// mirrored in C++, and the stub bytes as one STUB line for the Python twin). Prints one CHECK line per check and a
// RESULT line. No device, no Wine.
#include "occlusion_cull_core.h"
#include "occlusion_engine_core.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace x3m::occlusion_cull::core;
namespace census = x3m::cull_census::core;
static unsigned checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", what, ok ? "PASS" : "FAIL");
}
static bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

// A synthetic 32-bit engine image: words and strings at chosen addresses.
struct Image {
    std::map<std::uint32_t, std::vector<std::uint8_t>> blocks;
    void put(std::uint32_t at, const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        blocks[at] = std::vector<std::uint8_t>(b, b + n);
    }
    void word(std::uint32_t at, std::uint32_t v) { put(at, &v, 4); }
    bool read(std::uintptr_t at, void* out, std::size_t n) const {
        for (const auto& [base, bytes] : blocks)
            if (at >= base && at + n <= base + bytes.size()) {
                std::memcpy(out, bytes.data() + (at - base), n);
                return true;
            }
        return false;
    }
};

static void classification() {
    check(classify_name("ships\\split\\split_m7_cobra\\hull", 7) == Class::hull, "a ships\\ body is a hull piece");
    check(classify_name("ships\\props\\split_m1turretB_base", 5) == Class::part, "ships\\props\\ is a part");
    check(classify_name("SHIPS/Terran/turret_big", 9) == Class::part, "a turret body (upper case, '/') is a part");
    check(classify_name("ships\\argon\\argon_m2_gbarrel", 9) == Class::part &&
              classify_name("ships\\boron\\antenna01", 9) == Class::part &&
              classify_name("ships\\paranid\\weapon_mount", 9) == Class::part,
          "gun barrels, antennas and weapon mounts are parts");
    check(classify_name("effects\\engines\\jet_big", 9) == Class::effect &&
              classify_name("ships\\effects\\glow", 9) == Class::effect,
          "effects\\ paths are effects (never candidates)");
    check(classify_name("objects\\stations\\argon\\dock_big", 9) == Class::part,
          "a dock body is a part wherever it lives (the estimate's rule)");
    check(classify_name("objects\\stations\\argon\\factory", 9) == Class::other &&
              classify_name("environments\\sky01", 9) == Class::other && classify_name(nullptr, 9) == Class::other &&
              classify_name("", 9) == Class::other,
          "stations, environments, a null and an empty name are other");
    check(classify_name(nullptr, 901300005) == Class::part && classify_name("-", 909900001) == Class::part &&
              classify_name(nullptr, 901299999) == Class::other,
          "dock cut-scene inline ids are parts without a name; the neighbour id is not");
}

static void rectangle() {
    // Orthographic rows: x and y pass through, z' = z, w = 1.
    const float ortho[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const Box box{{-0.5f, -0.25f, 0.4f}, {0.5f, 0.25f, 0.6f}};
    Rect r{};
    check(test_rect(ortho, box, 200, 100, cmp_lessequal, &r) == RectStatus::ok, "a box in front: ok");
    check(near(r.c252[0], -0.5f - 0.01f) && near(r.c252[1], -0.25f - 0.02f) && near(r.c252[2], 0.4f) && r.c252[3] == 1.f &&
              near(r.c253[0], 1.f + 0.02f) && near(r.c253[1], 0.5f + 0.04f) && r.c253[2] == 0.f && r.c253[3] == 0.f,
          "the rectangle: the corners' extent inflated by one pixel (2/200, 2/100), the nearest z, w 1");
    check(near(r.w, 102.f) && near(r.h, 27.f) && near(r.cx, 0.f) && near(r.cy, 0.f), "pixel size 102 x 27, centred");
    Rect g{};
    check(test_rect(ortho, box, 200, 100, cmp_greaterequal, &g) == RectStatus::ok && near(g.c252[2], 0.6f),
          "a reversed depth (GREATEREQUAL) takes the largest z");
    check(test_rect(ortho, box, 200, 100, 8 /* ALWAYS */, &g) == RectStatus::zfunc &&
              test_rect(ortho, box, 200, 100, 3 /* EQUAL */, &g) == RectStatus::zfunc,
          "ALWAYS and EQUAL are refused");
    const Box crossing{{-0.5f, -0.25f, -0.1f}, {0.5f, 0.25f, 0.6f}};
    check(test_rect(ortho, crossing, 200, 100, cmp_less, &g) == RectStatus::near_plane,
          "a corner in front of the near plane (z < 0) is refused");
    // Perspective rows: w = z.
    const float persp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -0.1f, 0, 0, 1, 0};
    const Box behind{{-1, -1, -1}, {1, 1, 1}};
    check(test_rect(persp, behind, 200, 100, cmp_lessequal, &g) == RectStatus::near_plane,
          "a box reaching behind the eye is refused");
    const Box far{{-1, -1, 9}, {1, 1, 11}};
    check(test_rect(persp, far, 200, 100, cmp_lessequal, &g) == RectStatus::ok && near(g.c252[2], (9 - 0.1f) / 9) &&
              near(g.c252[0], -1.f / 9 - 0.01f),
          "perspective: the nearest corner's z/w and the widest x/w");
    check(test_rect(nullptr, far, 200, 100, cmp_less, &g) == RectStatus::degenerate &&
              test_rect(persp, far, 0, 100, cmp_less, &g) == RectStatus::degenerate,
          "no rows or no viewport: degenerate");
}

static void guard() {
    Rect was{}, now{};
    was.cx = 100, was.cy = 50, was.w = 40, was.h = 20;
    now = was;
    now.cx += 9; // under max(16, 10)
    check(stable(was, now), "a turret turning in place (9 px) is stable");
    now.cx = was.cx + 17;
    check(!stable(was, now), "17 px at 40 px is a jump");
    now = was;
    now.w = 80;
    check(!stable(was, now), "area x2 > (4/3)^2: unstable");
    now.w = 44;
    check(stable(was, now), "area x1.1: stable");
    Rect big = was;
    big.w = 400, big.h = 300;
    Rect moved = big;
    moved.cy += 90;
    check(stable(big, moved), "a large part may shift a quarter of its size (90 of 100)");
    now = was;
    now.w = 0;
    check(!stable(was, now), "an empty rectangle is never stable");
}

// One frame of the ring as the pass drives it: begin, the lag-2 then lag-1 reads (the given results per record index,
// none = not read), start.
static void frame_reads(Ring& ring, std::uint32_t f, const std::vector<Result>& lag2, const std::vector<Result>& lag1) {
    ring.begin(f);
    for (unsigned i = 0; i < ring.lag_count(2) && i < lag2.size(); ++i)
        if (lag2[i] != Result::none) ring.ingest(2, i, lag2[i]);
    for (unsigned i = 0; i < ring.lag_count(1) && i < lag1.size(); ++i)
        if (lag1[i] != Result::none) ring.ingest(1, i, lag1[i]);
    ring.start();
}

static void ring_bookkeeping() {
    static Ring ring; // ~110 KB: not on the stack
    ring.clear();
    Rect r{};
    r.w = r.h = 10;
    frame_reads(ring, 1, {}, {});
    check(ring.lag_count(1) == 0 && ring.lag_count(2) == 0, "frame 1: no older slot");
    Ring::Record* a = ring.reserve(0x100000001ull, r);
    Ring::Record* b = ring.reserve(0x200000002ull, r);
    check(a && b && ring.index_of(a) == 0 && ring.index_of(b) == 1, "records 0 and 1");
    a->issued = b->issued = true;
    a->skipped = true;
    ring.begin(2);
    check(ring.lag_count(1) == 2 && ring.lag_slot(1) == 1 && ring.lag_count(2) == 0, "frame 2 reads frame 1's slot (2 records)");
    ring.ingest(1, 0, Result::hidden);
    ring.ingest(1, 1, Result::visible);
    ring.start();
    const Ring::ResultSlot* res = ring.lookup(0x100000001ull);
    check(res && res->result == Result::hidden && res->age == 1, "lookup: the first draw read hidden one frame ago");
    res = ring.lookup(0x200000002ull);
    check(res && res->result == Result::visible && res->age == 1, "lookup: the second read visible");
    check(!ring.lookup(0x300000003ull), "an untested draw has no result");
    unsigned reserved = 0;
    while (ring.reserve(0x1000 + reserved, r)) ++reserved;
    check(reserved == pool_per_frame, "the frame's slot holds pool_per_frame records, then reserve() refuses");
    // Ready age: frame 3 issues key K; frame 4 reads it not ready; frame 5 polls it again (lag 2) ready-hidden while
    // frame 4's test of K is not ready: the decision uses the age-2 hidden result.
    ring.clear();
    frame_reads(ring, 3, {}, {});
    ring.reserve(77, r)->issued = true;
    frame_reads(ring, 4, {}, {Result::not_ready});
    res = ring.lookup(77);
    check(res && res->result == Result::not_ready, "frame 4: the frame-3 test not ready yet");
    ring.reserve(77, r)->issued = true;
    frame_reads(ring, 5, {Result::hidden}, {Result::not_ready});
    res = ring.lookup(77);
    check(res && res->result == Result::hidden && res->age == 2, "frame 5: the most recent ready result is two frames old");
    ring.reserve(77, r)->issued = true;
    frame_reads(ring, 6, {Result::not_ready}, {Result::visible});
    res = ring.lookup(77);
    check(res && res->result == Result::visible && res->age == 1, "frame 6: a ready one-frame result wins over age 2");
    ring.reserve(77, r)->issued = true;
    frame_reads(ring, 7, {Result::visible}, {Result::hidden});
    res = ring.lookup(77);
    check(res && res->result == Result::hidden && res->age == 1, "frame 7: the newer ready result wins");
    // A frame without candidates (8) leaves nothing to read for 9 at lag 1; frame 7's slot (lag 2) is still read.
    ring.reserve(78, r)->issued = true;
    frame_reads(ring, 9, {Result::hidden, Result::hidden}, {});
    check(ring.lag_count(1) == 0, "after a frame without candidates there is no lag-1 slot");
    ring.clear();
    frame_reads(ring, 12, {}, {});
    check(ring.lag_count(1) == 0 && ring.lag_count(2) == 0 && !ring.lookup(77), "clear() (Reset): nothing to read");
    // Duplicate keys within a frame: the later draw's result answers.
    frame_reads(ring, 20, {}, {});
    ring.reserve(42, r)->issued = true;
    ring.reserve(42, r)->issued = true;
    frame_reads(ring, 21, {}, {Result::hidden, Result::visible});
    res = ring.lookup(42);
    check(res && res->result == Result::visible, "a duplicate key: the later draw's result");
}

static void skip_rule() {
    Ring::ResultSlot slot{};
    slot.rect.w = slot.rect.h = 20;
    slot.result = Result::hidden;
    slot.age = 2;
    Rect now = slot.rect;
    check(may_skip(&slot, now, true), "hidden (age 2) + hull drawn + stable: skip");
    bool any = false;
    for (Result r : {Result::visible, Result::not_ready, Result::error, Result::none}) {
        slot.result = r;
        any = any || may_skip(&slot, now, true);
    }
    check(!any, "visible, not ready, error or untested: draw");
    slot.result = Result::hidden;
    check(!may_skip(&slot, now, false), "the hull did not draw this frame: draw");
    now.cx += 100;
    check(!may_skip(&slot, now, true), "the rectangle jumped: draw");
    check(!may_skip(nullptr, slot.rect, true), "no result: draw");
    check(draw_key(1, 2, 3, 4, 5) != draw_key(1, 2, 3, 5, 5) && draw_key(1, 2, 3, 4, 5) != draw_key(1, 2, 3, 4, 6) &&
              draw_key(1, 2, 3, 4, 5) != draw_key(2, 2, 3, 4, 5) && draw_key(1, 2, 3, 4, 5) == draw_key(1, 2, 3, 4, 5),
          "the draw key separates geometry, model and node");
    bool on = false;
    check(parse_mode(nullptr, &on) && on && parse_mode("", &on) && on && parse_mode("off", &on) && !on &&
              parse_mode("on", &on) && on && !parse_mode("yes", &on),
          "X3M_OCCLUSION_CULL: unset/empty/on = on, off = off, anything else refused");
}

static void classifier() {
    // Body table: global -> object with fixed/dynamic counts and a slot array; slot i's name pointer at +0x0c.
    Image img;
    const std::uint32_t global = 0x10000000, slots = 0x20000000;
    img.word(std::uint32_t(census::body_global_va), global);
    const std::int32_t head[3] = {census::body_fixed_count, 100, std::int32_t(slots)};
    img.put(global + census::body_fixed_count_offset, head, sizeof head);
    auto body = [&](std::int32_t id, std::uint32_t name_at, const char* name) {
        std::uint32_t index = 0;
        census::body_slot(id, census::body_fixed_count, 100, &index);
        img.word(slots + index * census::body_slot_stride + census::body_slot_name_offset, name_at);
        std::vector<char> page(0x100, 0); // the engine's names sit in readable heap pages
        std::memcpy(page.data(), name, std::strlen(name) + 1);
        img.put(name_at, page.data(), page.size());
    };
    body(10, 0x30000000, "ships\\split\\split_m7_cobra\\hull");
    body(11, 0x30001000, "ships\\props\\split_turret");
    body(12, 0x30002000, "effects\\engines\\jet");
    // Nodes: root 0x40000000 (no parent); hull 0x40001000 child of root; dummy 0x40002000 child of root; part
    // 0x40003000 child of the dummy; a stray part 0x40004000 whose chain never meets an owner.
    auto node = [&](std::uint32_t at, std::uint32_t parent, std::uint32_t model) {
        std::vector<std::uint8_t> bytes(0x200, 0);
        std::memcpy(bytes.data() + parent_offset, &parent, 4);
        std::memcpy(bytes.data() + model_offset, &model, 4);
        img.put(at, bytes.data(), bytes.size());
    };
    node(0x40000000, 0, 0);
    node(0x40001000, 0x40000000, 10);
    node(0x40002000, 0x40000000, 0);
    node(0x40003000, 0x40002000, 11);
    node(0x40004000, 0x40005000, 11);
    node(0x40005000, 0, 0);
    node(0x40006000, 0x40000000, 12);
    auto read = [&](std::uintptr_t at, void* out, std::size_t n) { return img.read(at, out, n); };
    static Classifier k;
    k.begin(1);
    std::uint32_t model = 0;
    bool hull = true;
    check(k.evaluate(read, census::body_global_va, 0x40003000, &model, &hull) == Class::part && model == 11 && !hull,
          "a part before its hull drew: no owner yet");
    k.begin(2);
    check(k.evaluate(read, census::body_global_va, 0x40001000, &model, &hull) == Class::hull && model == 10,
          "the hull draw registers itself and the root");
    check(k.evaluate(read, census::body_global_va, 0x40003000, &model, &hull) == Class::part && hull,
          "the part reaches the root through its dummy: its hull drew");
    check(k.evaluate(read, census::body_global_va, 0x40004000, &model, &hull) == Class::part && !hull,
          "a part of another object: no owner");
    check(k.evaluate(read, census::body_global_va, 0x40006000, &model, &hull) == Class::effect,
          "an engine jet is an effect");
    const unsigned walks = k.walks, resolves = k.resolves;
    k.evaluate(read, census::body_global_va, 0x40003000, &model, &hull);
    check(k.walks == walks && k.resolves == resolves && hull && model == 11,
          "a second draw of the node shares the memo (no walk, no resolve)");
    k.begin(3);
    check(k.evaluate(read, census::body_global_va, 0x40003000, &model, &hull) == Class::part && !hull &&
              k.resolves == resolves,
          "next frame before the hull: no owner, the class from the cache");
    check(k.evaluate(read, census::body_global_va, 0x40007000, &model, &hull) == Class::other && !hull,
          "an unreadable node is other");
    // A hull body on a top-level ship root (0x40010000, parent null): it owns itself only, so its own parts find it
    // and a part of another top-level object (0x40003000's root, whose hull has not drawn this frame) does not.
    node(0x40010000, 0, 10);
    node(0x40011000, 0x40010000, 0);
    node(0x40012000, 0x40011000, 11);
    // A hull under a node that is not top-level (0x40021000 -> 0x40020000 -> null): its parent is never an owner.
    node(0x40020000, 0, 0);
    node(0x40021000, 0x40020000, 0);
    node(0x40022000, 0x40021000, 10);
    node(0x40023000, 0x40021000, 0);
    node(0x40024000, 0x40023000, 11);
    k.begin(4);
    k.evaluate(read, census::body_global_va, 0x40010000, &model, &hull);
    k.evaluate(read, census::body_global_va, 0x40022000, &model, &hull);
    check(k.is_owner(0x40010000) && !k.is_owner(0) && !k.is_owner(0x40021000) && k.is_owner(0x40022000),
          "owners: a root hull itself; a nested hull itself, never its non-root parent");
    check(k.evaluate(read, census::body_global_va, 0x40012000, &model, &hull) == Class::part && hull,
          "a part of the root-hull ship finds its root");
    check(k.evaluate(read, census::body_global_va, 0x40003000, &model, &hull) == Class::part && !hull,
          "a part of another ship stops at its own root (no shared ancestor above the roots)");
    check(k.evaluate(read, census::body_global_va, 0x40024000, &model, &hull) == Class::part && !hull,
          "a part beside a nested hull: drawn (no owner registered above the hull)");
    // Ship roots: a hull under a top-level root reports the root; its parts report the same ship; a hull that is its
    // own top-level node reports itself; fresh only on the node's first evaluation of the frame.
    k.begin(5);
    std::uint32_t ship = 0;
    bool fresh = false;
    check(k.evaluate(read, census::body_global_va, 0x40001000, &model, &hull, &ship, &fresh) == Class::hull &&
              ship == 0x40000000 && fresh,
          "a hull's ship is its top-level parent (fresh at its first draw)");
    check(k.evaluate(read, census::body_global_va, 0x40001000, &model, &hull, &ship, &fresh) == Class::hull &&
              ship == 0x40000000 && !fresh,
          "the hull's second draw: memoised, not fresh");
    check(k.evaluate(read, census::body_global_va, 0x40003000, &model, &hull, &ship, &fresh) == Class::part && hull &&
              ship == 0x40000000,
          "its part (through the dummy) carries the same ship");
    check(k.evaluate(read, census::body_global_va, 0x40010000, &model, &hull, &ship, &fresh) == Class::hull &&
              ship == 0x40010000 &&
              k.evaluate(read, census::body_global_va, 0x40012000, &model, &hull, &ship, &fresh) == Class::part &&
              hull && ship == 0x40010000,
          "a top-level hull is its own ship, and its part's");
    check(k.evaluate(read, census::body_global_va, 0x40004000, &model, &hull, &ship, &fresh) == Class::part && !hull &&
              ship == 0,
          "a part without a drawn hull has no ship");
}

// Row-major clip rows P x V x W for a perspective camera at `eye` looking down +z, a model at `pos` scaled by `scale`
// and turned by `yaw` about y.
static void rows_of(float out[16], float eye_x, float eye_z, float pos_x, float pos_y, float pos_z, float yaw,
                    float scale) {
    const double n = 1.0, f = 30000.0, fx = 1.2, fy = 2.1;
    const double proj[16] = {fx, 0, 0, 0, 0, fy, 0, 0, 0, 0, f / (f - n), -n * f / (f - n), 0, 0, 1, 0};
    const double view[16] = {1, 0, 0, -eye_x, 0, 1, 0, 0, 0, 0, 1, -eye_z, 0, 0, 0, 1};
    const double c = std::cos(yaw) * scale, s = std::sin(yaw) * scale;
    const double world[16] = {c, 0, s, pos_x, 0, scale, 0, pos_y, -s, 0, c, pos_z, 0, 0, 0, 1};
    double pv[16], pvw[16];
    auto mul = [](const double* a, const double* b, double* o) {
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) {
                double sum = 0;
                for (int i = 0; i < 4; ++i) sum += a[r * 4 + i] * b[i * 4 + k];
                o[r * 4 + k] = sum;
            }
    };
    mul(proj, view, pv);
    mul(pv, world, pvw);
    for (int i = 0; i < 16; ++i) out[i] = float(pvw[i]);
}
// The part sits at a fixed offset in its ship: part rows = ship rows x local offset.
static void part_rows(float out[16], const float ship[16], float ox, float oy, float oz) {
    const float local[16] = {1, 0, 0, ox, 0, 1, 0, oy, 0, 0, 1, oz, 0, 0, 0, 1};
    multiply(ship, local, out);
}

static void reprojection() {
    float hull_then[16], hull_now[16], part_then[16], part_now[16], rel[16], estimate[16];
    double inverse[16];
    // A capital 2 km ahead, 600 m across; between the frames the camera moves 40 m sideways and 25 m forward, the ship
    // 30 m and turns 0.02 rad; the turret is 180 m off the ship's centre.
    rows_of(hull_then, 0.f, 0.f, 120.f, -40.f, 2000.f, 0.3f, 1.f);
    rows_of(hull_now, 40.f, 25.f, 150.f, -40.f, 2000.f, 0.32f, 1.f);
    part_rows(part_then, hull_then, 180.f, 60.f, -90.f);
    part_rows(part_now, hull_now, 180.f, 60.f, -90.f);
    check(invert(hull_then, inverse), "the hull's clip rows invert (perspective, far 30 km)");
    check(relative(hull_then, inverse, part_then, rel), "the part's rows relative to its hull pass the round trip");
    multiply(hull_now, rel, estimate);
    const Box box{{-8.f, -6.f, -8.f}, {8.f, 6.f, 8.f}};
    Rect actual{}, reprojected{}, stale{};
    const bool ok = test_rect(part_now, box, 5120, 1440, cmp_lessequal, &actual) == RectStatus::ok &&
                    test_rect(estimate, box, 5120, 1440, cmp_lessequal, &reprojected) == RectStatus::ok &&
                    test_rect(part_then, box, 5120, 1440, cmp_lessequal, &stale) == RectStatus::ok;
    const float dx = std::fabs(reprojected.cx - actual.cx), dy = std::fabs(reprojected.cy - actual.cy),
                dw = std::fabs(reprojected.w - actual.w), dz = std::fabs(reprojected.c252[2] - actual.c252[2]);
    std::printf("REPROJECT dx=%.5f dy=%.5f dw=%.5f dz=%.3g stale_dx=%.2f\n", dx, dy, dw, dz,
                std::fabs(stale.cx - actual.cx));
    check(ok && dx < 0.01f && dy < 0.01f && dw < 0.01f && dz < 1e-6f,
          "a rigid part's reprojected rectangle matches this frame's within 0.01 px and 1e-6 depth");
    check(std::fabs(stale.cx - actual.cx) > 10.f, "(the stale rectangle is off by more than 10 px in that motion)");
    const float singular[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0};
    check(!invert(singular, inverse), "a singular reference is refused (stale rectangles)");
    float bogus[16];
    std::memcpy(bogus, part_then, sizeof bogus);
    check(invert(hull_then, inverse) && relative(hull_then, inverse, part_then, rel), "(again)");
    // A wrong inverse (another frame's) fails the round trip.
    double other[16];
    invert(hull_now, other);
    check(!relative(hull_then, other, part_then, rel), "an inverse that does not belong to the reference is refused");
    bool parsed = true;
    unsigned k = 0;
    parsed = parse_retest(nullptr, &k) && k == retest_default && parse_retest("", &k) && k == 8 &&
             parse_retest("1", &k) && k == 1 && parse_retest("64", &k) && k == 64 && !parse_retest("0", &k) &&
             !parse_retest("65", &k) && !parse_retest("8x", &k) && !parse_retest("-1", &k) && !parse_retest("0008", &k);
    check(parsed, "X3M_OCCLUSION_CULL_RETEST: unset/empty = 8, 1..64, anything else refused");
}

// The batcher across frames as the pass drives it: hulls, parts, the ship's block (plan), the issued tests and the
// results read back the next frame.
static void batching() {
    static Batcher b; // ~0.6 MB: not on the stack
    b.retest = 4;
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const std::uint32_t ship_a = 0x5000, ship_b = 0x6000;
    // Ship A: parts 1..6 (1..3 will read hidden, 4..6 visible); ship B: parts 7..8, drawn interleaved with A's.
    auto box_of = [](unsigned i) {
        const float x = -0.9f + 0.2f * float(i % 8);
        return Box{{x, 0.1f, 0.5f}, {x + 0.1f, 0.2f, 0.6f}};
    };
    auto rect_of = [&](unsigned i, const float* rows) {
        Rect r{};
        test_rect(rows, box_of(i), 256, 256, cmp_lessequal, &r);
        return r;
    };
    BlockItem items[pool_per_frame];
    unsigned tests[9]{}, entries[9]{}, off_phase = 0, per_frame_visible[32]{};
    std::uint32_t frame = 0;
    // One frame: A's hull, A's parts 1..3, B's hull, B's part 7, A's parts 4..6, B's part 8. Block at each ship's first
    // part. results: the result each tested part reads (applied next frame).
    Result results[9]{};
    PlanStats total{};
    unsigned blocks = 0, planned_first = 0;
    auto run_frame = [&](const float* rows_a, bool new_part9, std::uint64_t hull_key_a) {
        ++frame;
        b.begin(frame);
        // the previous frame's tests' results arrive at the frame's first part
        for (unsigned i = 1; i <= 8; ++i)
            if (b.parts[entries[i]].tested == frame - 1 && b.parts[entries[i]].key == 100 + i)
                b.on_result(std::uint16_t(entries[i]), 100 + i, results[i]);
        b.note_hull(ship_a, 0xa0, hull_key_a, rows_a);
        auto part = [&](unsigned i, std::uint32_t ship, const float* rows) {
            const Rect r = rect_of(i, rows);
            entries[i] = b.note_part(100 + i, ship, box_of(i), r, rows, true, 7 * i);
            if (b.block_pending(ship)) {
                PlanStats st{};
                std::uint32_t sig = 0;
                const unsigned n = b.plan(ship, 256, 256, cmp_lessequal, items, pool_per_frame, &st, &sig);
                if (ship == ship_a && !planned_first) planned_first = n;
                for (unsigned t = 0; t < n; ++t) {
                    b.tested(items[t], sig, std::uint16_t(t));
                    for (unsigned j = 1; j <= 8; ++j)
                        if (items[t].key == 100 + j) {
                            ++tests[j];
                            const bool visible = j >= 4 && j != 7;
                            if (visible && frame >= 3 && frame <= 12) {
                                ++per_frame_visible[frame];
                                if (frame % b.retest != b.parts[entries[j]].phase) ++off_phase;
                            }
                        }
                }
                total.retest_skipped += st.retest_skipped;
                total.stale += st.stale;
                ++blocks;
            }
        };
        for (unsigned i = 1; i <= 3; ++i) part(i, ship_a, rows_a);
        b.note_hull(ship_b, 0xb0, 0xb0b0, identity);
        part(7, ship_b, identity);
        for (unsigned i = 4; i <= 6; ++i) part(i, ship_a, rows_a);
        part(8, ship_b, identity);
        if (new_part9) part(0, ship_a, rows_a);
    };
    for (unsigned i = 1; i <= 8; ++i) results[i] = i <= 3 || i == 7 ? Result::hidden : Result::visible;
    run_frame(identity, false, 0xa0a0);
    check(blocks == 2 && planned_first == 0 && tests[1] == 0, "frame 1: blocks issue nothing (no previous list)");
    run_frame(identity, false, 0xa0a0);
    unsigned all = 0;
    for (unsigned i = 1; i <= 8; ++i) all += tests[i];
    check(blocks == 4 && all == 8, "frame 2: one block per ship tests all of last frame's parts, interleaved or not");
    for (unsigned f = 3; f <= 12; ++f) run_frame(identity, false, 0xa0a0);
    // Hidden parts (1-3, 7) every frame from 2 on: 11 tests; visible parts (4-6, 8) at frame 2 (no result yet), then
    // only on their phase frames (frame mod 4 == phase): 1 + 10/4 rounded by phase over frames 3..12.
    check(tests[1] == 11 && tests[2] == 11 && tests[3] == 11 && tests[7] == 11, "hidden parts are tested every frame");
    bool cadence = off_phase == 0;
    unsigned phases[4]{};
    for (unsigned j : {4u, 5u, 6u, 8u}) {
        unsigned expected = 1;
        for (unsigned f = 3; f <= 12; ++f) expected += f % 4 == b.parts[entries[j]].phase;
        cadence = cadence && tests[j] == expected;
        ++phases[b.parts[entries[j]].phase];
    }
    std::printf("PHASES %u %u %u %u\n", phases[0], phases[1], phases[2], phases[3]);
    check(cadence, "visible parts are re-tested only on their own phase frame (once per `retest` frames)");
    check(retest_phase(1, 2, 8) == retest_phase(1, 2, 8) && retest_phase(1, 2, 1) == 0 && retest_phase(5, 9, 8) < 8,
          "the phase is a fixed function of node and model, below retest");
    unsigned spread[8]{};
    for (unsigned node = 0; node < 800; ++node) ++spread[retest_phase(0x40000000u + node * 0x40u, 900000 + node % 7, 8)];
    unsigned lo = ~0u, hi = 0;
    for (unsigned v : spread) lo = v < lo ? v : lo, hi = v > hi ? v : hi;
    std::printf("SPREAD min=%u max=%u of 100\n", lo, hi);
    check(lo >= 75 && hi <= 125, "800 nodes spread over 8 phases within 25 % of 100 each");
    // 40 parts turning visible in the same frame: their phases balance over the 8 phases (5 each).
    {
        static Batcher v;
        v.retest = 8;
        v.begin(5);
        unsigned per[8]{};
        for (unsigned i = 0; i < 40; ++i) {
            const std::uint16_t e = v.note_part(0x40000000ull + i * 0x150, 0x7000, box_of(1), rect_of(1, identity), identity,
                                                true, 900000 + i);
            v.on_result(e, 0x40000000ull + i * 0x150, Result::visible);
            ++per[v.parts[e].phase];
        }
        unsigned plo = ~0u, phi = 0;
        for (unsigned c : per) plo = c < plo ? c : plo, phi = c > phi ? c : phi;
        check(plo == 5 && phi == 5, "40 parts turning visible together: 5 per phase (balanced, then fixed)");
    }
    check(total.retest_skipped > 0 && total.stale == 0, "the cadence's untested parts are counted; no stale test");
    // A new part (0) is drawn untested its first frame and joins the next block.
    run_frame(identity, true, 0xa0a0); // frame 13
    check(b.parts[entries[0]].tested == 0, "a part first seen this frame is not tested");
    run_frame(identity, true, 0xa0a0); // frame 14
    check(b.parts[entries[0]].tested == frame, "it joins the next frame's block");
    // The hull changes (another hull draw key): every part of the ship is tested at the next block.
    const unsigned v5 = tests[5];
    run_frame(identity, true, 0xa1a1); // frame 15
    check(tests[5] == v5 + 1, "a changed hull signature tests the visible parts at once");
    // A visible part that moved: tested at once.
    run_frame(identity, true, 0xa1a1); // frame 16
    const unsigned v6 = tests[6];
    float moved[16];
    std::memcpy(moved, identity, sizeof moved);
    moved[3] = 0.3f; // ship A moved right by 0.3 (38 px): every part of A unstable against its last test
    run_frame(moved, true, 0xa1a1); // frame 17: the hull rows moved with the parts; the reprojection sees it
    check(tests[6] == v6 + 1, "a part whose reprojected rectangle moved is tested at once");
    // Reset: every part's results forgotten, all tested at the next block.
    b.reset_results();
    const unsigned v8 = tests[8];
    run_frame(moved, true, 0xa1a1);
    check(tests[8] == v8 + 1 && tests[6] == v6 + 2, "after reset_results every part is tested at its next block");
    // No reference hull rows this frame (rows unknown): the stale rectangle, counted.
    total.stale = 0;
    ++frame;
    b.begin(frame);
    b.note_hull(ship_a, 0xa0, 0xa1a1, nullptr);
    entries[1] = b.note_part(101, ship_a, box_of(1), rect_of(1, moved), moved, true);
    b.note_hull(ship_b, 0xb0, 0xb0b0, identity);
    entries[7] = b.note_part(107, ship_b, box_of(7), rect_of(7, identity), identity, true);
    entries[8] = b.note_part(108, ship_b, box_of(8), rect_of(8, identity), identity, true);
    PlanStats st{};
    std::uint32_t sig = 0;
    unsigned n = b.plan(ship_a, 256, 256, cmp_lessequal, items, pool_per_frame, &st, &sig);
    check(n >= 3 && st.stale == n - 1, "no reference rows: last frame's rectangles (all but the block's own part stale)");
    check(!b.block_pending(ship_a) && b.plan(ship_a, 256, 256, cmp_lessequal, items, pool_per_frame, &st, &sig) == 0,
          "one block per ship per frame");
    // A reversed depth at the block refuses the parts recorded under LESSEQUAL.
    ++frame;
    b.begin(frame);
    b.note_hull(ship_b, 0xb0, 0xb0b0, identity);
    entries[7] = b.note_part(107, ship_b, box_of(7), rect_of(7, identity), identity, false);
    st = PlanStats{};
    n = b.plan(ship_b, 256, 256, cmp_greaterequal, items, pool_per_frame, &st, &sig);
    check(st.refused >= 1, "a part listed under another depth direction is refused at the block");
    // Records and results: the ring carries the batcher's slot, on_result ignores a reused slot's old key.
    b.on_result(std::uint16_t(entries[8]), 999, Result::hidden);
    check(b.parts[entries[8]].last != Result::hidden || b.parts[entries[8]].key == 999, "a stale record's result is ignored");
    b.on_result(std::uint16_t(entries[8]), 108, Result::error);
    check(b.parts[entries[8]].last == Result::none, "an error forgets the last result (tested at the next block)");
    check(b.record_of(std::uint16_t(entries[2]), 102) == -1, "no record this frame for a part not tested this frame");
    // Two keys on one probe chain: A takes the home slot, B the next. A expires while B stays live; B's next draw must
    // find its own entry (phase and last result kept), not take A's expired slot ahead of it.
    {
        static Batcher c;
        c.retest = 8;
        const std::uint64_t key_a = 0x0000000000001234ull, key_b = 0x0000000100001235ull; // same slot_of input
        std::uint32_t f = 1;
        c.begin(f);
        const std::uint16_t ea = c.note_part(key_a, 0x7000, box_of(1), rect_of(1, identity), identity, true, 1);
        const std::uint16_t eb = c.note_part(key_b, 0x7000, box_of(2), rect_of(2, identity), identity, true, 2);
        c.on_result(eb, key_b, Result::visible);
        const unsigned phase_b = c.parts[eb].phase;
        for (unsigned k = 0; k <= part_expiry + 1; ++k) {
            c.begin(++f);
            c.note_part(key_b, 0x7000, box_of(2), rect_of(2, identity), identity, true, 2);
        }
        c.begin(++f);
        const std::uint16_t again = c.note_part(key_b, 0x7000, box_of(2), rect_of(2, identity), identity, true, 2);
        unsigned copies = 0;
        for (const auto& part : c.parts) copies += part.seen && part.key == key_b ? 1 : 0;
        check(ea + 1 == eb && again == eb && copies == 1 && c.parts[eb].last == Result::visible &&
                  c.parts[eb].phase == phase_b,
              "a live entry behind an expired slot of its chain is found (one copy, phase and result kept)");
        const std::uint16_t a2 = c.note_part(key_a, 0x7000, box_of(1), rect_of(1, identity), identity, true, 1);
        check(a2 == ea && c.parts[a2].last == Result::none, "the expired slot is reused for its own key, fresh");
    }
}

// The engine-side skip's core: the verdict table's rule (skipped only after a fully skipped previous frame, the guards,
// the withheld skip on the node's phase, the fall-back to drawn), the ledger, the window and the stub bytes.
static void engine_table() {
    namespace oe = x3m::occlusion_cull::engine;
    oe::Mode m = oe::Mode::off;
    check(oe::parse_mode(nullptr, &m) && m == oe::Mode::on && oe::parse_mode("", &m) && m == oe::Mode::on &&
              oe::parse_mode("on", &m) && m == oe::Mode::on && oe::parse_mode("off", &m) && m == oe::Mode::off &&
              oe::parse_mode("engine", &m) && m == oe::Mode::engine && !oe::parse_mode("Engine", &m) &&
              !oe::parse_mode("engine ", &m) && !oe::parse_mode("1", &m),
          "engine: X3M_OCCLUSION_CULL unset/empty/on = on, off, engine; anything else refused");
    check(!std::strcmp(oe::mode_name(oe::Mode::engine), "engine") && !std::strcmp(oe::mode_name(oe::Mode::on), "on") &&
              !std::strcmp(oe::mode_name(oe::Mode::off), "off"),
          "engine: mode names");
    // The window: 1/64 of the largest |component|, floor 1, all-zero refused, saturating at the int32 ends.
    std::int32_t lo[3], hi[3];
    const std::int32_t pos[3] = {64000, -1280, 5};
    check(oe::window(pos, lo, hi) && lo[0] == 63750 && hi[0] == 64250 && lo[1] == -1530 && hi[1] == -1030 && lo[2] == -245 &&
              hi[2] == 255,
          "engine: window = position +- max|component| / 256 on every axis");
    const std::int32_t small[3] = {0, 3, -2}, zero[3] = {0, 0, 0}, ends[3] = {2147483647, -2147483647 - 1, 0};
    check(oe::window(small, lo, hi) && lo[0] == -1 && hi[0] == 1 && lo[1] == 2 && hi[1] == 4 && lo[2] == -3 && hi[2] == -1,
          "engine: window floor of 1 unit");
    check(!oe::window(zero, lo, hi), "engine: an all-zero position (fields not live) has no window");
    check(oe::window(ends, lo, hi) && hi[0] == 2147483647 && lo[1] == -2147483647 - 1, "engine: the window saturates");
    bool slots_ok = true;
    for (std::uint32_t node = 0x40000000u; node < 0x40000000u + 0x4000u; node += 4)
        slots_ok = slots_ok && oe::hash_slot(node) < oe::table_slots;
    check(slots_ok, "engine: the hash is a home slot below table_slots");
    // The ledger: draws per node per frame, skips, the position read once, a full probe window drops.
    static oe::Ledger l;
    unsigned reads = 0;
    const std::int32_t live[3] = {1000, 2000, 3000};
    auto read = [&](std::uintptr_t at, void* out, std::size_t n) {
        ++reads;
        if (at != 0x40000100u + oe::position_offset || n != 12) return false;
        std::memcpy(out, live, 12);
        return true;
    };
    l.begin(5, 0x41c68200u);
    oe::Ledger::Slot* a = l.draw(0x40000100u);
    l.skipped(a, 77, read);
    oe::Ledger::Slot* a2 = l.draw(0x40000100u);
    l.skipped(a2, 77, read);
    oe::Ledger::Slot* b = l.draw(0x40000200u);
    oe::Ledger::Slot* c = l.draw(0x40000300u);
    l.skipped(c, 78, read); // the read fails for this node: pos_ok false
    check(a == a2 && a->draws == 2 && a->skipped == 2 && a->model == 77 && a->pos_ok && a->pos[2] == 3000 && reads == 2 &&
              b && b->draws == 1 && !b->skipped && c->skipped == 1 && !c->pos_ok && l.touched_n == 3 && l.view == 0x41c68200u,
          "engine: ledger counts draws and skips per node, reads the position once per node per frame");
    check(!l.draw(0) && !l.dropped, "engine: a null node is not a slot");
    l.begin(6, 0x41c68200u);
    check(l.touched_n == 0 && l.draw(0x40000100u)->draws == 1, "engine: a new frame starts every node afresh");
    {
        // ledger_probe + 1 nodes on one home chain: the last is dropped (never published).
        static oe::Ledger full;
        full.begin(1, 1);
        const std::uint32_t home = 0;
        unsigned seated = 0;
        std::uint32_t node = 4;
        for (unsigned n = 0; n < oe::ledger_probe + 1; ++n) {
            while ((oe::hash_slot(node) & (oe::ledger_slots - 1)) != home) node += 4;
            seated += full.draw(node) ? 1 : 0;
            node += 4;
        }
        check(seated == oe::ledger_probe && full.dropped == 1, "engine: a full ledger chain drops the node (counted)");
    }
    // The table's publish rule over a duty cycle: frame 10's ledger says A fully skipped (2 draws), B partially (1 of
    // 2), C drawn, D fully skipped without a position, E fully skipped: published for frame 11 = A and E.
    static oe::Table t;
    static oe::Ledger f;
    const std::uint32_t A = 0x40001000u, B = 0x40001100u, C = 0x40001200u, D = 0x40001300u, E = 0x40001400u;
    const std::int32_t pa[3] = {-32000, 500, 128000}, pe[3] = {7, 7, 7};
    auto read2 = [&](std::uintptr_t at, void* out, std::size_t n) {
        if (n != 12) return false;
        if (at == A + oe::position_offset) return std::memcpy(out, pa, 12), true;
        if (at == E + oe::position_offset) return std::memcpy(out, pe, 12), true;
        return false;
    };
    const unsigned K = 4;
    auto frame10 = [&](std::uint32_t stamp) {
        f.begin(stamp, 0x41c68200u);
        f.skipped(f.draw(A), 11, read2);
        f.skipped(f.draw(A), 11, read2);
        f.skipped(f.draw(B), 12, read2);
        f.draw(B);
        f.draw(C);
        f.skipped(f.draw(D), 14, read2);
        f.skipped(f.draw(E), 15, read2);
    };
    // A stamp on nobody's phase: search one for A and E.
    std::uint32_t s11 = 11;
    while (s11 % K == retest_phase(A, 11, K) || s11 % K == retest_phase(E, 15, K)) ++s11;
    frame10(s11 - 1);
    oe::PublishStats st{};
    check(t.publish(f, s11, K, &st) == 2 && st.candidates == 5 && st.published == 2 && st.partial == 1 && st.no_position == 1 &&
              st.withheld == 0 && st.overflow == 0 && t.used_n == 2,
          "engine: published = nodes with every draw skipped and a live position (partial, drawn, no position excluded)");
    const oe::Entry* ea = t.find(A);
    check(ea && ea->model == 11 && ea->stamp == s11 && ea->draws == 2 && ea->lo_x == -32500 && ea->hi_x == -31500 &&
              ea->lo_y == 0 && ea->hi_y == 1000 && ea->lo_z == 127500 && ea->hi_z == 128500 && !t.find(B) && !t.find(C) && !t.find(D),
          "engine: the entry carries model, stamp, draw count and the window (500 = 128000 / 256)");
    check(t.lookup(A, 11, pa, s11) == oe::Verdict::skip && t.lookup(E, 15, pe, s11) == oe::Verdict::skip,
          "engine: the stub's lookup skips a listed node at its position");
    check(t.lookup(A, 12, pa, s11) == oe::Verdict::model, "engine: guard: a reused address with another model id is not skipped");
    check(t.lookup(A, 11, pa, s11 + 1) == oe::Verdict::stamp && t.lookup(A, 11, pa, s11 - 1) == oe::Verdict::stamp,
          "engine: guard: an entry of another frame is not skipped");
    const std::int32_t edge[3] = {-31500, 1000, 127500}, out1[3] = {-31499, 500, 128000}, out2[3] = {-32000, 500, 128501};
    check(t.lookup(A, 11, edge, s11) == oe::Verdict::skip && t.lookup(A, 11, out1, s11) == oe::Verdict::position &&
              t.lookup(A, 11, out2, s11) == oe::Verdict::position,
          "engine: guard: a position on the window's edge skips, one unit outside draws");
    check(t.lookup(B, 12, pa, s11) == oe::Verdict::miss && t.lookup(C, 13, pa, s11) == oe::Verdict::miss &&
              t.lookup(0x40009999u, 0, pa, s11) == oe::Verdict::miss,
          "engine: an unlisted node misses");
    // The duty cycle: in frame s11 A is engine-skipped (no draws); its next publish lists nobody; in s11 + 1 it draws
    // through the proxy and is listed again for s11 + 2. A node drawn (not skipped) in a frame is not listed.
    f.begin(s11, 0x41c68200u); // engine-skipped: no draw of A; E drawn this time (proxy-visible)
    f.draw(E);
    st = oe::PublishStats{};
    check(t.publish(f, s11 + 1, K, &st) == 0 && !t.find(A) && !t.find(E) && t.lookup(A, 11, pa, s11 + 1) == oe::Verdict::miss,
          "engine: a node with no draws last frame (engine-skipped) or drawn last frame is not listed: it draws");
    f.begin(s11 + 1, 0x41c68200u);
    f.skipped(f.draw(A), 11, read2);
    st = oe::PublishStats{};
    const bool forced_now = (s11 + 2) % K == retest_phase(A, 11, K);
    t.publish(f, s11 + 2, K, &st);
    check(forced_now ? (st.withheld == 1 && !t.find(A)) : (st.published == 1 && t.lookup(A, 11, pa, s11 + 2) == oe::Verdict::skip),
          "engine: proxy-skipped again: listed for the next frame (unless on its phase)");
    // The withheld skip: over K consecutive stamps a node fully skipped every frame is withheld exactly once, on
    // stamp % K == retest_phase(node, model, K); with K = 1 never.
    unsigned forced = 0, published = 0, forced_at = ~0u;
    for (std::uint32_t s = 100; s < 100 + K; ++s) {
        f.begin(s - 1, 0x41c68200u);
        f.skipped(f.draw(A), 11, read2);
        st = oe::PublishStats{};
        t.publish(f, s, K, &st);
        forced += st.withheld;
        published += st.published;
        if (st.withheld) forced_at = s % K;
    }
    check(forced == 1 && published == K - 1 && forced_at == retest_phase(A, 11, K),
          "engine: withheld once every retest frames, on the node's own phase (the visible re-test's stagger)");
    st = oe::PublishStats{};
    check(t.publish(f, 100 + K, 1, &st) == 1 && !st.withheld, "engine: retest 1: never withheld");
    // Overflow: table_probe + 1 nodes on one home slot: table_probe seated and found, one overflow.
    {
        static oe::Ledger o;
        static oe::Table ot;
        o.begin(200, 1);
        std::uint32_t node = 0x40000004u, nodes[oe::table_probe + 1];
        const std::int32_t p[3] = {10, 20, 30};
        auto read3 = [&](std::uintptr_t, void* out, std::size_t) { return std::memcpy(out, p, 12), true; };
        for (unsigned n = 0; n < oe::table_probe + 1; ++n) {
            while (oe::hash_slot(node) != 300) node += 4;
            nodes[n] = node;
            o.skipped(o.draw(node), 1, read3);
            node += 4;
        }
        st = oe::PublishStats{};
        unsigned found = 0;
        const unsigned seated = ot.publish(o, 201, 1, &st);
        for (unsigned n = 0; n < oe::table_probe + 1; ++n) found += ot.lookup(nodes[n], 1, p, 201) == oe::Verdict::skip ? 1 : 0;
        check(seated == oe::table_probe && st.overflow == 1 && found == oe::table_probe && ot.used_n == oe::table_probe,
              "engine: a full probe chain overflows (counted); every seated node is found by the bounded probe");
        ot.clear();
        found = 0;
        for (unsigned n = 0; n < oe::table_probe + 1; ++n) found += ot.find(nodes[n]) ? 1 : 0;
        check(!found && !ot.used_n && !ot.entries[300].node, "engine: clear() empties exactly the used entries");
    }
    // The stub bytes for the Python twin, and the structural facts the twin checks too.
    unsigned char out[oe::stub_length];
    const oe::StubWords w{0x10002000u, 0x10002004u, 0x10002008u, 0x10003000u, 0x1000200cu, 0x10002010u, 0x10002014u,
                          0x10002018u, 0x1000201cu, 0x10002020u};
    oe::encode_stub(0x10000000u, w, 0x0047d2c3u, 0x10000118u, out);
    std::printf("STUB ");
    for (unsigned i = 0; i < oe::stub_length; ++i) std::printf("%02x", out[i]);
    std::printf("\n");
    std::uint32_t v = 0;
    check(oe::stub_length == 285 && oe::stub_visits == 20 && oe::stub_probe == 38 && oe::stub_continue == 99 && oe::stub_hit == 105 &&
              oe::stub_reject_model == 201 && oe::stub_reject_stamp == 213 && oe::stub_reject_position == 225 && oe::stub_skip == 237 &&
              oe::stub_replay == 255 && oe::stub_cull == 280,
          "engine: stub layout");
    std::memcpy(&v, out + 2, 4);
    check(out[0] == 0x83 && out[1] == 0x3d && v == 0x10002000u && out[6] == 0 && out[7] == 0x74 && 9 + out[8] == oe::stub_continue,
          "engine: cmp dword [armed],0; je continue");
    std::memcpy(&v, out + 10, 4);
    check(out[9] == 0xa1 && v == 0x10002004u && !std::memcmp(out + 14, "\x39\x44\x24\x28\x75", 5) && 20 + out[19] == oe::stub_continue,
          "engine: mov eax,[sector_view]; cmp [esp+0x28],eax; jne continue");
    std::memcpy(&v, out + 22, 4);
    check(out[20] == 0xff && out[21] == 0x05 && v == 0x1000200cu, "engine: inc dword [visits] on the sector-view path only");
    check(!std::memcmp(out + 26, "\x69\xc7\xb1\x79\x37\x9e\xc1\xe8\x17\x6b\xc0\x30", 12) && oe::hash_multiplier == 2654435761u &&
              oe::hash_slot(0x41c68200u) == ((0x41c68200u * 2654435761u) >> 23),
          "engine: imul eax,edi,0x9e3779b1 (hash_multiplier); shr eax,23; imul eax,eax,48");
    bool probes = true;
    for (unsigned p = 0; p < oe::table_probe; ++p) {
        const unsigned at = oe::stub_probe + p * oe::stub_probe_length;
        std::memcpy(&v, out + at + 2, 4);
        probes = probes && out[at] == 0x8b && out[at + 1] == 0x88 && v == 0x10003000u && out[at + 6] == 0x3b && out[at + 7] == 0xcf &&
                 out[at + 8] == 0x74 && at + 10 + out[at + 9] == oe::stub_hit;
        if (p + 1 < oe::table_probe)
            probes = probes && out[at + 10] == 0x85 && out[at + 11] == 0xc9 && out[at + 12] == 0x74 &&
                     at + 14 + out[at + 13] == oe::stub_continue && !std::memcmp(out + at + 14, "\x83\xc0\x30", 3);
    }
    check(probes, "engine: four probes: mov ecx,[eax+table]; cmp ecx,edi; je hit; test ecx,ecx; je continue; add eax,48");
    std::memcpy(&v, out + 101, 4);
    check(out[99] == 0xff && out[100] == 0x25 && v == 0x10000118u, "engine: continue: jmp [next]");
    std::memcpy(&v, out + 113, 4);
    check(!std::memcmp(out + 105, "\x8b\x8f\x40\x01\x00\x00\x3b\x88", 8) && v == 0x10003004u && out[117] == 0x75 && 119 + out[118] == oe::stub_reject_model,
          "engine: hit: mov ecx,[edi+0x140]; cmp ecx,[eax+table+4]; jne reject_model");
    std::memcpy(&v, out + 121, 4);
    check(out[119] == 0x8b && out[120] == 0x0d && v == 0x10002008u && out[125] == 0x3b && out[126] == 0x88 && out[131] == 0x75 &&
              133 + out[132] == oe::stub_reject_stamp,
          "engine: mov ecx,[stamp]; cmp ecx,[eax+table+8]; jne reject_stamp");
    bool axes = true;
    for (unsigned axis = 0; axis < 3; ++axis) {
        const unsigned at = 133 + axis * 22;
        std::uint32_t off = 0, lo_f = 0, hi_f = 0;
        std::memcpy(&off, out + at + 2, 4);
        std::memcpy(&lo_f, out + at + 8, 4);
        std::memcpy(&hi_f, out + at + 16, 4);
        axes = axes && out[at] == 0x8b && out[at + 1] == 0x8f && off == 0xf0 + 4 * axis && out[at + 6] == 0x3b && out[at + 7] == 0x88 &&
               lo_f == 0x10003000u + 12 + 8 * axis && out[at + 12] == 0x7c && at + 14 + out[at + 13] == oe::stub_reject_position &&
               out[at + 14] == 0x3b && out[at + 15] == 0x88 && hi_f == 0x10003000u + 16 + 8 * axis && out[at + 20] == 0x7f &&
               at + 22 + out[at + 21] == oe::stub_reject_position;
    }
    check(axes && out[199] == 0xeb && 201 + out[200] == oe::stub_skip,
          "engine: per axis: mov ecx,[edi+0xf0+4a]; cmp lo (jl reject); cmp hi (jg reject); then jmp skip");
    bool rejects = true;
    const std::uint32_t counters[3] = {0x10002018u, 0x1000201cu, 0x10002020u};
    for (unsigned r = 0; r < 3; ++r) {
        const unsigned at = oe::stub_reject_model + 12 * r;
        std::uint32_t cnt = 0, nxt = 0;
        std::memcpy(&cnt, out + at + 2, 4);
        std::memcpy(&nxt, out + at + 8, 4);
        rejects = rejects && out[at] == 0xff && out[at + 1] == 0x05 && cnt == counters[r] && out[at + 6] == 0xff && out[at + 7] == 0x25 && nxt == 0x10000118u;
    }
    check(rejects, "engine: reject_model/stamp/position: inc dword [counter]; jmp [next]");
    std::uint32_t parts = 0, draws_f = 0, draws_c = 0;
    std::memcpy(&parts, out + 239, 4);
    std::memcpy(&draws_f, out + 245, 4);
    std::memcpy(&draws_c, out + 251, 4);
    check(out[237] == 0xff && out[238] == 0x05 && parts == 0x10002010u && out[243] == 0x8b && out[244] == 0x88 && draws_f == 0x10003000u + 36 &&
              out[249] == 0x01 && out[250] == 0x0d && draws_c == 0x10002014u,
          "engine: skip: inc dword [skipped_parts]; mov ecx,[eax+table+36]; add [skipped_draws],ecx");
    check(!std::memcmp(out + 255, "\x8b\x4f\x18\x85\xc9\x8b\x87\xd8\x01\x00\x00\x74\x0c\x8b\x89\xd8\x01\x00\x00\x3b\xc8\x7e\x02\x8b\xc1", 25),
          "engine: the engine's limit computation replayed (0x0047d2a2..0x0047d2b9)");
    std::memcpy(&v, out + 281, 4);
    check(out[280] == 0xe9 && 0x10000000u + oe::stub_length + v == 0x0047d2c3u, "engine: jmp cull target");
    // No byte of the stub writes EDX, ESI, EDI, EBX, EBP or ESP: every opcode is from the documented list (the twin
    // decodes the same list); here the only register-writing forms are mov/imul/shr/add to EAX and mov to ECX.
    unsigned writes_other = 0;
    for (unsigned i = 0; i + 1 < oe::stub_length; ++i)
        if (out[i] == 0x8b && (out[i + 1] & 0x38) != 0x08 && out[i + 1] != 0xc1 && out[i + 1] != 0x4f && out[i + 1] != 0x87 && out[i + 1] != 0x89 && out[i + 1] != 0x88 && out[i + 1] != 0x8f && out[i + 1] != 0x0d)
            ++writes_other;
    check(writes_other == 0, "engine: mov destinations are EAX or ECX only (the replay's forms included)");
}

int main() {
    classification();
    rectangle();
    guard();
    ring_bookkeeping();
    skip_rule();
    classifier();
    reprojection();
    batching();
    engine_table();
    std::printf("RESULT checks=%u failed=%u\n", checks, failures);
    return failures ? 1 : 0;
}
