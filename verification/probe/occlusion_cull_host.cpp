// Host driver of the occlusion cull's pure core (src/proxy/occlusion_cull_core.h), compiled with the host compiler by
// verification/analysis/test_occlusion_cull.py: the body-name classification, the test rectangle (inflation, nearest
// depth, refusals), the stability guard, the ring's bookkeeping (pool, previous slot, lookups across a frame gap and a
// clear), the skip rule, the draw key and the classifier over a synthetic engine image (hull owners, the part walk,
// the per-node memo). Prints one CHECK line per check and a RESULT line. No device, no Wine.
#include "occlusion_cull_core.h"
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
}

int main() {
    classification();
    rectangle();
    guard();
    ring_bookkeeping();
    skip_rule();
    classifier();
    std::printf("RESULT checks=%u failed=%u\n", checks, failures);
    return failures ? 1 : 0;
}
