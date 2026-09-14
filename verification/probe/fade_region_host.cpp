// Host driver for src/proxy/fade_region_math.h (verification/analysis/
// test_fade_region.py). Random boxes, rows and viewports: every interior point
// projected on the CPU must land inside the derived rectangle; hand cases print
// the rectangle for exact comparison. No D3D, no Windows.
#include "../../src/proxy/fade_region_math.h"
#include "../../src/proxy/fade_region_core.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <chrono>
#include <random>
#include <string>
#include <vector>

using namespace x3m::fade_region;

namespace {
struct Sample { double rows[16]; };
// Pixel index of a projected point under both D3D9 conventions (integer pixel
// centres: round; half-integer centres: floor). Both must lie in the rectangle.
bool covered(const Rect& rect, const Viewport& v, double sx, double sy, bool* clipped) {
    const Rect full = full_rect(v);
    const std::int32_t xs[2] = {std::int32_t(std::floor(sx)), std::int32_t(std::floor(sx + .5))};
    const std::int32_t ys[2] = {std::int32_t(std::floor(sy)), std::int32_t(std::floor(sy + .5))};
    *clipped = true;
    for (auto x : xs) for (auto y : ys) {
        if (!contains(full, x, y)) continue;
        *clipped = false;
        if (!contains(rect, x, y)) return false;
    }
    return true;
}
int random_mode(unsigned seed, unsigned cases, unsigned points) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit(0, 1);
    unsigned failures = 0;
    for (unsigned c = 0; c < cases; ++c) {
        Viewport v{};
        const unsigned kind = c % 8;
        v.x = kind == 3 ? unsigned(unit(rng) * 64) : 0; v.y = kind == 3 ? unsigned(unit(rng) * 64) : 0;
        v.width = 1 + unsigned(unit(rng) * 2047); v.height = 1 + unsigned(unit(rng) * 2047);
        Box box{};
        for (unsigned a = 0; a < 3; ++a) {
            box.centre[a] = (unit(rng) * 4 - 2);
            box.half[a] = kind == 4 ? 0.0 : kind == 5 ? 2.0 : unit(rng) * 2;
        }
        if (kind == 5) box.centre[0] = box.centre[1] = box.centre[2] = 0; // |p| <= 2, the encoding limit
        float rows[16];
        if (kind == 6) {
            for (auto& r : rows) r = float(unit(rng) * 20 - 10); // arbitrary rows: w may change sign
        } else {
            // Perspective-like rows: rotation-free scale, translation, w = z + d.
            const double s = 0.25 + unit(rng) * 8, d = kind == 7 ? unit(rng) * 3 - 1 : 2.5 + unit(rng) * 400;
            const double tx = unit(rng) * 4 - 2, ty = unit(rng) * 4 - 2;
            const float m[16] = {float(s), 0, 0, float(tx), 0, float(s), 0, float(ty), 0, 0, .5f, float(.5 * d), 0, 0, 1, float(d)};
            std::memcpy(rows, m, sizeof rows);
        }
        const bool jitter = (c & 1) != 0;
        if (jitter) jitter_rows(rows, float(unit(rng) - .5), float(unit(rng) - .5), v.width, v.height);
        Rect rect{};
        const Reason reason = project_box(rows, box, v, &rect);
        unsigned inside = 0, outside = 0, clipped = 0, wneg = 0;
        if (reason == Reason::Bound) {
            for (unsigned n = 0; n < points; ++n) {
                double p[3];
                for (unsigned a = 0; a < 3; ++a) p[a] = box.centre[a] + (unit(rng) * 2 - 1) * box.half[a];
                double clip[4];
                for (unsigned k = 0; k < 4; ++k) clip[k] = double(rows[4 * k]) * p[0] + double(rows[4 * k + 1]) * p[1] + double(rows[4 * k + 2]) * p[2] + double(rows[4 * k + 3]);
                if (!(clip[3] > 0)) { ++wneg; continue; }
                const double sx = v.x + (clip[0] / clip[3] + 1) * v.width * .5, sy = v.y + (1 - clip[1] / clip[3]) * v.height * .5;
                bool clip_flag = false;
                if (covered(rect, v, sx, sy, &clip_flag)) { if (clip_flag) ++clipped; else ++inside; }
                else ++outside;
            }
        }
        if (outside || wneg) ++failures;
        std::printf("CASE index=%u kind=%u jitter=%u reason=%u rect=%d,%d,%d,%d viewport=%u,%u,%u,%u inside=%u outside=%u clipped=%u w_nonpositive=%u\n",
                    c, kind, jitter, unsigned(reason), rect.left, rect.top, rect.right, rect.bottom, v.x, v.y, v.width, v.height, inside, outside, clipped, wneg);
    }
    std::printf("RESULT cases=%u failures=%u\n", cases, failures);
    return failures ? 1 : 0;
}
// --case rows(16) centre(3) half(3) viewport(4) rows_known bound_known fill_solid [jx jy]
int case_mode(int argc, char** argv) {
    if (argc < 2 + 16 + 3 + 3 + 4 + 3) { std::fprintf(stderr, "case arguments\n"); return 2; }
    float rows[16]; Box box{}; Viewport v{};
    int i = 2;
    auto number = [&](const char* text) { return std::string(text) == "nan" ? std::numeric_limits<double>::quiet_NaN() : std::string(text) == "inf" ? std::numeric_limits<double>::infinity() : std::strtod(text, nullptr); };
    for (auto& r : rows) r = float(number(argv[i++]));
    for (auto& c : box.centre) c = number(argv[i++]);
    for (auto& h : box.half) h = number(argv[i++]);
    v.x = unsigned(std::strtoul(argv[i++], nullptr, 10)); v.y = unsigned(std::strtoul(argv[i++], nullptr, 10));
    v.width = unsigned(std::strtoul(argv[i++], nullptr, 10)); v.height = unsigned(std::strtoul(argv[i++], nullptr, 10));
    const bool rows_known = std::atoi(argv[i++]) != 0, bound_known = std::atoi(argv[i++]) != 0, fill = std::atoi(argv[i++]) != 0;
    if (i + 1 < argc) jitter_rows(rows, float(number(argv[i])), float(number(argv[i + 1])), v.width, v.height);
    const Region region = derive(rows_known ? rows : nullptr, bound_known, box, v, fill, Rect{0, 0, 64, 64});
    std::printf("REGION bound=%u reason=%u pad=%u rect=%d,%d,%d,%d f=%.9f\n", region.bound, unsigned(region.reason), region.pad,
                region.rect.left, region.rect.top, region.rect.right, region.rect.bottom, area_fraction(region.rect, v));
    return 0;
}

// --table: fake game memory (descriptor, part, subset records) and a fake
// buffer registry drive BoundTable::resolve through the miss/hit/poison/
// eviction paths. Addresses and identities are synthetic; nothing is
// dereferenced.
namespace table {
constexpr std::uintptr_t descriptor = 0x00a00000, part = 0x00b00000, records = 0x00c00000;
constexpr std::uintptr_t vb = 0x0d000000, ib = 0x0d000010, other_vb = 0x0d000020, other_ib = 0x0d000030;
struct Span { std::uintptr_t base; std::size_t size; unsigned char* bytes; };
Span spans[3];
unsigned reads = 0;
bool read_span(std::uintptr_t address, void* out, std::size_t size) noexcept {
    ++reads;
    for (const auto& s : spans)
        if (s.bytes && address >= s.base && address + size <= s.base + s.size) { std::memcpy(out, s.bytes + (address - s.base), size); return true; }
    return false;
}
std::uintptr_t scope_descriptor = descriptor; std::uint32_t scope_depth = 1;
bool scope(std::uintptr_t* d, std::uint32_t* depth) noexcept { *d = scope_descriptor; *depth = scope_depth; return scope_depth != 0; }
struct Content { std::uintptr_t wrapper; std::uint64_t revision; bool known; };
Content contents[4] = {{vb, 7, true}, {ib, 3, true}, {other_vb, 1, true}, {other_ib, 1, true}};
bool content(std::uintptr_t wrapper, std::uint64_t* revision) noexcept {
    for (const auto& c : contents) if (c.wrapper == wrapper) { if (!c.known) return false; *revision = c.revision; return true; }
    return false;
}
unsigned char descriptor_bytes[0x20]{}, part_bytes[0x80]{}, record_bytes[0x1a8 * 3]{};
void put32(unsigned char* at, std::uint32_t v) { std::memcpy(at, &v, 4); }
void reset_memory(unsigned records_count = 2, std::uintptr_t back = descriptor, std::uintptr_t match_vb = vb, std::uintptr_t match_ib = ib) {
    std::memset(descriptor_bytes, 0, sizeof descriptor_bytes); std::memset(part_bytes, 0, sizeof part_bytes); std::memset(record_bytes, 0, sizeof record_bytes);
    put32(descriptor_bytes + 0, part); put32(descriptor_bytes + 4, 1u << 16); put32(descriptor_bytes + 8, records_count); put32(descriptor_bytes + 0xc, records);
    const std::int32_t aabb[7] = {4 * 100, 4 * -200, 4 * 50, 0, 4 * 300, 4 * 150, 4 * 75}; // centre, pad, half (4 x int16 units)
    std::memcpy(part_bytes + 0x40, aabb, sizeof aabb);
    put32(part_bytes + 0x64, std::uint32_t(back));
    put32(record_bytes + 0 * 0x1a8 + 0xc, other_vb); put32(record_bytes + 0 * 0x1a8 + 0x10, other_ib);
    put32(record_bytes + 1 * 0x1a8 + 0xc, std::uint32_t(match_vb)); put32(record_bytes + 1 * 0x1a8 + 0x10, std::uint32_t(match_ib));
    spans[0] = {descriptor, sizeof descriptor_bytes, descriptor_bytes};
    spans[1] = {part, sizeof part_bytes, part_bytes};
    spans[2] = {records, sizeof record_bytes, record_bytes};
    scope_descriptor = descriptor; scope_depth = 1;
    contents[0] = {vb, 7, true}; contents[1] = {ib, 3, true};
}
void print(const char* label, const Result& r, const BoundTable& t) {
    std::printf("TABLE %s status=%u name=%s hit=%u poisoned_now=%u evicted=%u depth=%u part=%08x aabb=%d,%d,%d,%d,%d,%d vb_rev=%llu ib_rev=%llu centre=%.6f,%.6f,%.6f half=%.6f,%.6f,%.6f used=%u poisoned=%u evictions=%u reads=%u\n",
                label, unsigned(r.status), status_name(r.status), r.hit, r.poisoned_now, r.evicted, r.depth, unsigned(r.part),
                r.aabb[0], r.aabb[1], r.aabb[2], r.aabb[3], r.aabb[4], r.aabb[5], (unsigned long long)r.vb_revision, (unsigned long long)r.ib_revision,
                r.box.centre[0], r.box.centre[1], r.box.centre[2], r.box.half[0], r.box.half[1], r.box.half[2], t.used(), t.poisoned(), t.evictions(), reads);
    reads = 0;
}
int run() {
    const Environment env{&read_span, &scope, &content, nullptr};
    const Query query{101, 202, vb, ib};
    BoundTable t;
    print("no_table", t.resolve(query, env), t);
    if (!t.reserve()) return 3;
    scope_depth = 0; reset_memory(); scope_depth = 0;
    print("no_scope", t.resolve(query, env), t);
    reset_memory();
    print("peek_miss", t.peek(query, env), t);        // same five reads, nothing inserted: used stays 0
    print("miss_learn", t.resolve(query, env), t);   // reads: head, aabb, back, record0, record1
    print("hit", t.resolve(query, env), t);           // no reads
    print("peek_hit", t.peek(query, env), t);         // served from the entry, no reads, no stamp
    print("wrapper_learn", t.resolve(Query{109, 209, vb, ib}, env), t);
    print("wrapper_mismatch", t.resolve(Query{109, 209, other_vb, other_ib}, env), t); // same ids, other identities: poison, never a hit
    reset_memory();
    put32(part_bytes + 0x40, std::uint32_t(4 * 33000)); // centre 2.01 > 2: outside the |p| <= 2 domain
    print("out_of_domain", t.resolve(Query{108, 202, vb, ib}, env), t);
    reset_memory();
    reset_memory(3, part + 4);                         // back-link mismatch on a different VB id
    print("back_link", t.resolve(Query{102, 202, vb, ib}, env), t);
    reset_memory(2, descriptor, other_vb, other_ib);   // no record holds the bound pair
    print("no_record", t.resolve(Query{103, 203, vb, ib}, env), t);
    reset_memory();
    contents[0].revision = 8;                          // VB rewritten after first sight
    print("peek_invalid", t.peek(query, env), t);     // reports Poisoned without poisoning: poisoned counter unchanged
    print("poison_revision", t.resolve(query, env), t);
    contents[0].revision = 7;
    print("poisoned_stays", t.resolve(query, env), t);
    print("peek_poisoned", t.peek(query, env), t);    // poisoned entry: Poisoned, hit, no change
    print("ib_mismatch_new", t.resolve(Query{104, 204, vb, ib}, env), t);   // learns a fresh entry (different VB id)
    print("ib_mismatch_hit", t.resolve(Query{104, 205, vb, ib}, env), t);   // same VB id, different IB id: poison
    contents[1].known = false;
    print("content_unknown", t.resolve(Query{105, 202, vb, ib}, env), t);
    contents[1].known = true;
    scope_descriptor = descriptor + 0x10;               // scoped descriptor differs from the entry
    spans[0] = {descriptor + 0x10, sizeof descriptor_bytes, descriptor_bytes};
    put32(part_bytes + 0x64, std::uint32_t(descriptor + 0x10));
    print("descriptor_mismatch", t.resolve(Query{106, 202, vb, ib}, env), t); // fresh learn under the other descriptor
    scope_descriptor = descriptor; spans[0] = {descriptor, sizeof descriptor_bytes, descriptor_bytes}; put32(part_bytes + 0x64, std::uint32_t(descriptor));
    print("descriptor_poison", t.resolve(Query{106, 202, vb, ib}, env), t);
    spans[1].bytes = nullptr;
    print("read_failed", t.resolve(Query{107, 202, vb, ib}, env), t);
    reset_memory();
    // Eviction: fill one probe window with distinct ids hashing to the same start slot.
    std::uint64_t ids[BoundTable::probe_window + 1]; unsigned found = 0;
    const auto slot = [](std::uint64_t id) { return std::uint32_t((id * 0x9e3779b97f4a7c15ull) >> 32) & (BoundTable::capacity - 1); };
    const std::uint32_t target = slot(1000);
    for (std::uint64_t id = 1000; found < BoundTable::probe_window + 1 && id < 10000000; ++id) if (slot(id) == target) ids[found++] = id;
    BoundTable e; e.reserve();
    for (unsigned i = 0; i < BoundTable::probe_window; ++i) { const Result r = e.resolve(Query{ids[i], 202, vb, ib}, env); if (r.status != Status::Bound || r.evicted) return 4; }
    contents[0].revision = 9; (void)e.resolve(Query{ids[1], 202, vb, ib}, env); contents[0].revision = 7; // poison ids[1]
    print("peek_window_full", e.peek(Query{ids[BoundTable::probe_window], 202, vb, ib}, env), e); // a miss on a full window derives without evicting
    print("window_full", e.resolve(Query{ids[BoundTable::probe_window], 202, vb, ib}, env), e); // evicts ids[0], the oldest unpoisoned
    print("evicted_relearn", e.resolve(Query{ids[0], 202, vb, ib}, env), e);                     // ids[0] is gone: a miss that evicts again
    print("poisoned_kept", e.resolve(Query{ids[1], 202, vb, ib}, env), e);                       // ids[1] still poisoned
    // Eviction happens after the reads succeed: a failed read must not evict.
    spans[1].bytes = nullptr;
    print("failed_read_no_evict", e.resolve(Query{ids[BoundTable::probe_window] + 1, 202, vb, ib}, env), e);
    reset_memory();
    e.clear(); print("cleared", e.resolve(query, env), e);
    return 0;
}
} // namespace table

// --prefix: the locked-prefix scan, checkpoints, cover, table and the
// resolve_locked_prefix binding (src/proxy/locked_prefix_core.h, step B of
// screen-emission-region.md) on host memory. Every case prints one PREFIX
// line; random cases prove the superset property (every drawn vertex inside
// the covering box) with a garbage tail.
namespace prefix_mode {
using namespace x3m::fade_region::prefix;
using x3m::fade_region::Box;
constexpr unsigned quad = 6;
std::vector<float> buffer(std::size_t vertices) { return std::vector<float>(vertices * stride / sizeof(float)); }
void put(std::vector<float>& b, std::size_t i, float x, float y, float z) { b[i * 6] = x; b[i * 6 + 1] = y; b[i * 6 + 2] = z; }
void print(const char* label, Lookup status, const Box& box, std::uint32_t checkpoint, std::uint64_t revision, const Table& t, unsigned extra = 0) {
    std::printf("PREFIX %s status=%u name=%s box=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f checkpoint=%u revision=%llu used=%u evictions=%llu publications=%llu extra=%u\n",
                label, unsigned(status), lookup_name(status), box.centre[0], box.centre[1], box.centre[2], box.half[0], box.half[1], box.half[2],
                checkpoint, (unsigned long long)revision, t.used(), (unsigned long long)t.evictions(), (unsigned long long)t.publications(), extra);
}
Lookup look(const Table& t, std::uintptr_t key, std::uint32_t count, Box& box, std::uint32_t& checkpoint, std::uint64_t& revision) {
    box = Box{}; checkpoint = 0; revision = 0;
    return t.lookup(key, count, &box, &revision, &checkpoint);
}
// Fake production binding for resolve_locked_prefix: one published record.
Table* env_table = nullptr;
bool env_prefix(std::uintptr_t wrapper, std::uint32_t vertex_count, Box* box, std::uint64_t* revision, std::uint32_t* checkpoint, unsigned* refusal) noexcept {
    const Lookup l = env_table->lookup(wrapper, vertex_count, box, revision, checkpoint);
    *refusal = unsigned(l);
    return l == Lookup::Bound;
}
int run(unsigned seed, unsigned cases) {
    Table t; Box box{}; std::uint32_t cp = 0; std::uint64_t rev = 0;
    // Checkpoint math: vertex i at (i, -i, 2i); 200 vertices -> 3 checkpoints.
    auto b = buffer(200);
    for (unsigned i = 0; i < 200; ++i) put(b, i, float(i), -float(i), 2.f * i);
    print("math_unknown", look(t, 1, 96, box, cp, rev), box, cp, rev, t);
    // An unmarked buffer's lock leaves no record; the draw marks it first.
    print("unmarked_lock_ignored", (t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7), t.finish_lock(1, 7), look(t, 1, 96, box, cp, rev)), box, cp, rev, t);
    t.mark(1);
    print("marked_unknown", look(t, 1, 96, box, cp, rev), box, cp, rev, t);
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7);
    print("math_pending", look(t, 1, 96, box, cp, rev), box, cp, rev, t);
    const std::uint32_t scanned = t.finish_lock(1, 7);
    print("math_96", look(t, 1, 96, box, cp, rev), box, cp, rev, t, scanned);     // checkpoint 0: [0,96)
    print("math_97", look(t, 1, 97, box, cp, rev), box, cp, rev, t);              // checkpoint 1: [0,192)
    print("math_192", look(t, 1, 192, box, cp, rev), box, cp, rev, t);
    print("math_193", look(t, 1, 193, box, cp, rev), box, cp, rev, t);            // checkpoint 2: [0,200)
    print("math_200", look(t, 1, 200, box, cp, rev), box, cp, rev, t);
    print("math_1", look(t, 1, 1, box, cp, rev), box, cp, rev, t);                // still checkpoint 0 (95 stale vertices)
    print("empty", look(t, 1, 0, box, cp, rev), box, cp, rev, t);
    print("beyond", look(t, 1, 201, box, cp, rev), box, cp, rev, t);
    print("beyond_max", look(t, 1, max_vertices + 1, box, cp, rev), box, cp, rev, t);
    // Revision: a second lock makes the record pending (revision 2); a
    // failed Unlock invalidates; a non-DISCARD lock is invalid outright.
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7);
    print("relock_pending", look(t, 1, 96, box, cp, rev), box, cp, rev, t);
    t.finish_lock(1, 7); t.invalidate(1);
    print("unlock_failed", look(t, 1, 96, box, cp, rev), box, cp, rev, t);
    t.begin_lock(1, false, nullptr, 0, 7); t.finish_lock(1, 7);
    print("non_discard", look(t, 1, 96, box, cp, rev), box, cp, rev, t);
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7);
    print("thread_mismatch", (t.finish_lock(1, 8), look(t, 1, 96, box, cp, rev)), box, cp, rev, t);
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7); t.finish_lock(1, 7);
    print("relearned", look(t, 1, 200, box, cp, rev), box, cp, rev, t);
    t.erase(1);
    print("erased", look(t, 1, 96, box, cp, rev), box, cp, rev, t);
    // A nested lock (Lock while locked) invalidates the record through both
    // Unlocks; the next fresh lock publishes again.
    t.mark(3);
    t.begin_lock(3, true, b.data(), b.size() * sizeof(float), 7); t.begin_lock(3, true, b.data(), b.size() * sizeof(float), 7);
    t.finish_lock(3, 7);
    print("nested_first_unlock", look(t, 3, 96, box, cp, rev), box, cp, rev, t);
    t.finish_lock(3, 7);
    print("nested_invalid", look(t, 3, 96, box, cp, rev), box, cp, rev, t);
    t.begin_lock(3, true, b.data(), b.size() * sizeof(float), 7); t.finish_lock(3, 7);
    print("nested_relearned", look(t, 3, 96, box, cp, rev), box, cp, rev, t);
    t.erase(3);
    // Stale tail: 100 valid vertices in [-1, 1], garbage from 100 on.
    auto g = buffer(300);
    auto fill = [&](float tail) { for (unsigned i = 0; i < 300; ++i) { const float v = i < 100 ? (i % 2 ? 1.f : -1.f) : tail; put(g, i, v, v, v); } };
    const auto publish = [&](std::uintptr_t key) { t.mark(key); t.begin_lock(key, true, g.data(), g.size() * sizeof(float), 7); t.finish_lock(key, 7); };
    fill(std::numeric_limits<float>::quiet_NaN()); publish(2);
    print("tail_nan_100", look(t, 2, 100, box, cp, rev), box, cp, rev, t);        // checkpoint 1 holds NaN: refused
    print("tail_nan_96", look(t, 2, 96, box, cp, rev), box, cp, rev, t);          // checkpoint 0 clean: bound
    for (unsigned i = 100; i < 192; ++i) put(g, i, 0, 0, 0); publish(2);
    print("tail_nan_beyond", look(t, 2, 100, box, cp, rev), box, cp, rev, t);     // NaN only from 192: bound
    fill(std::numeric_limits<float>::infinity()); publish(2);
    print("tail_inf_100", look(t, 2, 100, box, cp, rev), box, cp, rev, t);
    fill(1e30f); publish(2);
    print("tail_absurd_100", look(t, 2, 100, box, cp, rev), box, cp, rev, t);     // beyond world_limit: refused
    fill(world_limit); publish(2);
    print("tail_limit_100", look(t, 2, 100, box, cp, rev), box, cp, rev, t);      // exactly the limit: bound, larger box
    fill(1e6f); publish(2);
    print("tail_huge_100", look(t, 2, 100, box, cp, rev), box, cp, rev, t);       // finite garbage: larger box
    print("tail_huge_96", look(t, 2, 96, box, cp, rev), box, cp, rev, t);         // exact prefix: tight box
    // A NaN inside the valid prefix is refused whatever the count.
    fill(0); put(g, 5, std::numeric_limits<float>::quiet_NaN(), 0, 0); publish(2);
    print("prefix_nan", look(t, 2, 96, box, cp, rev), box, cp, rev, t);
    // Partial trailing vertex: 10 vertices plus 7 bytes scan as 10.
    Scan partial{};
    print("length_partial", Lookup(scan(g.data(), 10 * stride + 7, &partial)), box, partial.vertices, partial.blocks, t);
    Scan capped{};
    auto big = buffer(max_vertices + 10);
    print("length_capped", Lookup(scan(big.data(), big.size() * sizeof(float), &capped)), box, capped.vertices, capped.blocks, t);
    // Eviction: capacity + 1 distinct keys; the oldest (key 100) goes.
    Table e;
    for (unsigned k = 0; k <= Table::capacity; ++k) { e.mark(100 + k); e.begin_lock(100 + k, true, b.data(), b.size() * sizeof(float), 7); e.finish_lock(100 + k, 7); }
    print("evicted_oldest", look(e, 100, 96, box, cp, rev), box, cp, rev, e);
    print("evicted_kept", look(e, 101, 96, box, cp, rev), box, cp, rev, e);
    e.clear();
    print("cleared", look(e, 101, 96, box, cp, rev), box, cp, rev, e);
    // resolve_locked_prefix through the Environment binding.
    Table r; r.mark(0xd000); r.begin_lock(0xd000, true, b.data(), b.size() * sizeof(float), 7); r.finish_lock(0xd000, 7); env_table = &r;
    const x3m::fade_region::Environment env{&table::read_span, &table::scope, &table::content, &env_prefix};
    const x3m::fade_region::Environment no_prefix{&table::read_span, &table::scope, &table::content, nullptr};
    auto res = [&](const char* label, const x3m::fade_region::Result& out) {
        std::printf("RESOLVE %s status=%u name=%s source=%u bound=%u refusal=%u checkpoint=%u revision=%llu box=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", label, unsigned(out.status),
                    x3m::fade_region::status_name(out.status), unsigned(out.source), out.status == x3m::fade_region::Status::Bound, out.prefix_refusal, out.checkpoint,
                    (unsigned long long)out.vb_revision, out.box.centre[0], out.box.centre[1], out.box.centre[2], out.box.half[0], out.box.half[1], out.box.half[2]);
    };
    res("bound", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 200, env));
    res("no_vb", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{0, 0, 0, 0}, 200, env));
    res("zero_count", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 0, env));
    res("no_binding", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 200, no_prefix));
    res("unknown_buffer", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd001, 0}, 200, env));
    res("beyond", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 201, env));
    // Random superset cases: N quads (6 vertices each) inside a random box,
    // finite garbage tail; the covering box contains every drawn vertex and
    // equals the true extent exactly when N*6 is a multiple of 96.
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit(0, 1);
    unsigned failures = 0, exact = 0, larger = 0;
    auto v = buffer(max_vertices);
    for (unsigned c = 0; c < cases; ++c) {
        const unsigned quads = 1 + unsigned(unit(rng) * (max_vertices / quad));
        const unsigned count = quads * quad;
        double centre[3], half[3];
        for (unsigned a = 0; a < 3; ++a) { centre[a] = unit(rng) * 2000 - 1000; half[a] = unit(rng) * 100; }
        const float tail = float(unit(rng) * 4000 - 2000);
        for (unsigned i = 0; i < max_vertices; ++i) put(v, i, tail, -tail, tail * .5f);
        float lo[3] = {3e38f, 3e38f, 3e38f}, hi[3] = {-3e38f, -3e38f, -3e38f};
        for (unsigned i = 0; i < count; ++i) {
            float p[3];
            for (unsigned a = 0; a < 3; ++a) { p[a] = float(centre[a] + (unit(rng) * 2 - 1) * half[a]); lo[a] = p[a] < lo[a] ? p[a] : lo[a]; hi[a] = p[a] > hi[a] ? p[a] : hi[a]; }
            put(v, i, p[0], p[1], p[2]);
        }
        Table s; s.mark(9); s.begin_lock(9, true, v.data(), v.size() * sizeof(float), 1); s.finish_lock(9, 1);
        const Lookup l = look(s, 9, count, box, cp, rev);
        bool ok = l == Lookup::Bound && cp == (count + interval - 1) / interval - 1;
        bool tight = true;
        for (unsigned a = 0; a < 3 && ok; ++a) {
            const double bl = box.centre[a] - box.half[a], bh = box.centre[a] + box.half[a];
            ok = bl <= lo[a] && bh >= hi[a];
            tight = tight && bl == double(lo[a]) && bh == double(hi[a]);
        }
        for (unsigned i = 0; i < count && ok; ++i)
            for (unsigned a = 0; a < 3; ++a) { const double x = v[i * 6 + a]; ok = ok && x >= box.centre[a] - box.half[a] && x <= box.centre[a] + box.half[a]; }
        if (!ok) ++failures;
        else if (tight) ++exact; else ++larger;
        if (count % interval == 0 && !tight) ++failures; // a whole number of checkpoints has no stale vertex
    }
    // Scan cost on this host: the full 6144-vertex window.
    Scan timing{};
    volatile float sink = 0;
    const auto begin = std::chrono::steady_clock::now();
    constexpr unsigned iterations = 2000;
    for (unsigned i = 0; i < iterations; ++i) { v[i % 6] = float(i); scan(v.data(), v.size() * sizeof(float), &timing); sink = sink + timing.at[63].hi[0]; }
    const double ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count()) / iterations;
    std::printf("PREFIX_RANDOM cases=%u failures=%u exact=%u larger=%u scan_ns=%.0f vertices=%u\n", cases, failures, exact, larger, ns, timing.vertices);
    return failures ? 1 : 0;
}
} // namespace prefix_mode
// --near seed cases points: near-plane clipping (screen-emission-region.md,
// step B). Perspective-like rows whose near plane (clip z = 0, at w = d_n)
// cuts through, before or behind random boxes. Every interior point with
// clip z >= 0 (the D3D-visible half-space) must land inside the clipped
// rectangle; a box with every corner behind must be BehindNear; a box with no
// corner behind must give exactly the unclipped rectangle.
int near_mode(unsigned seed, unsigned cases, unsigned points) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit(0, 1);
    unsigned failures = 0;
    for (unsigned c = 0; c < cases; ++c) {
        Viewport v{0, 0, 16 + unsigned(unit(rng) * 2032), 16 + unsigned(unit(rng) * 1064)};
        Box box{};
        for (unsigned a = 0; a < 3; ++a) { box.centre[a] = unit(rng) * 8 - 4; box.half[a] = unit(rng) * 3; }
        // x' = s x + tx, y' = s y + ty, z' = k (z + d - zn), w = z + d: near plane at w = zn.
        const double s = 0.25 + unit(rng) * 8, d = unit(rng) * 10 - 5, zn = 0.01 + unit(rng) * 3, k = 0.05 + unit(rng);
        // Every third case is a world-coordinate case: the box sits at ~1e3 to
        // ~1e5 and the rows carry the cancelling translation, exactly the
        // bullet situation of screen-emission-bullet-bound.md section 4. The
        // clip values are the same as at the origin; the fp32 dp4 error is not.
        const double world = (c % 3 == 0) ? std::pow(10.0, 3 + unit(rng) * 2) : 0.0;
        for (unsigned a = 0; a < 3; ++a) box.centre[a] += world;
        const double tx = unit(rng) * 4 - 2, ty = unit(rng) * 4 - 2;
        const float rows[16] = {float(s), 0, 0, float(tx - s * world), 0, float(s), 0, float(ty - s * world),
                                0, 0, float(k), float(k * (d - zn) - k * world), 0, 0, 1, float(d - world)};
        Rect rect{}, plain{};
        NearClip cut{true, 0};
        unsigned pad = 0;
        const Reason reason = project_box(rows, box, v, &rect, &cut, &pad);
        const Reason plain_reason = project_box(rows, box, v, &plain);
        unsigned behind_corners = 0;
        for (unsigned corner = 0; corner < 8; ++corner) {
            double p[3];
            for (unsigned a = 0; a < 3; ++a) p[a] = box.centre[a] + ((corner >> a) & 1u ? 1 : -1) * (box.half[a] + half_float_expansion);
            const double z = rows[8] * p[0] + rows[9] * p[1] + rows[10] * p[2] + rows[11];
            if (z < 0) ++behind_corners;
        }
        bool fail = false;
        if (cut.clipped != behind_corners) fail = true;
        if ((reason == Reason::BehindNear) != (behind_corners == 8)) fail = true;
        if (behind_corners == 0 && (reason != plain_reason || std::memcmp(&rect, &plain, sizeof rect) != 0)) fail = true;
        unsigned inside = 0, outside = 0, clipped = 0, invisible = 0, outside_fp32 = 0;
        if (reason == Reason::Bound) {
            for (unsigned n = 0; n < points; ++n) {
                double p[3];
                for (unsigned a = 0; a < 3; ++a) p[a] = box.centre[a] + (unit(rng) * 2 - 1) * box.half[a];
                double clip[4];
                for (unsigned kk = 0; kk < 4; ++kk) clip[kk] = double(rows[4 * kk]) * p[0] + double(rows[4 * kk + 1]) * p[1] + double(rows[4 * kk + 2]) * p[2] + double(rows[4 * kk + 3]);
                if (clip[2] < 0) { ++invisible; continue; } // behind the near plane: D3D never rasterises it
                if (!(clip[3] > 0)) { fail = true; continue; }
                const double sx = v.x + (clip[0] / clip[3] + 1) * v.width * .5, sy = v.y + (1 - clip[1] / clip[3]) * v.height * .5;
                bool clip_flag = false;
                if (covered(rect, v, sx, sy, &clip_flag)) { if (clip_flag) ++clipped; else ++inside; }
                else ++outside;
                // The pad's actual duty: the hardware evaluates the same dp4
                // in fp32. Perturb each screen-reaching clip component by the
                // whole error bound eps_dp4 * sum|terms| in every direction;
                // the padded rectangle must still cover the point.
                double err[4] = {0, 0, 0, 0};
                for (unsigned kk = 0; kk < 4; ++kk) {
                    if (kk == 2) continue;
                    const double sum = std::fabs(double(rows[4 * kk]) * p[0]) + std::fabs(double(rows[4 * kk + 1]) * p[1]) +
                                       std::fabs(double(rows[4 * kk + 2]) * p[2]) + std::fabs(double(rows[4 * kk + 3]));
                    err[kk] = eps_dp4 * sum;
                }
                for (int sx_sign = -1; sx_sign <= 1; sx_sign += 2)
                    for (int sy_sign = -1; sy_sign <= 1; sy_sign += 2)
                        for (int sw_sign = -1; sw_sign <= 1; sw_sign += 2) {
                            const double w = clip[3] + sw_sign * err[3];
                            if (!(w > 0)) continue; // fp32 would put it behind the eye; not rasterised as this point
                            const double qx = v.x + ((clip[0] + sx_sign * err[0]) / w + 1) * v.width * .5;
                            const double qy = v.y + (1 - (clip[1] + sy_sign * err[1]) / w) * v.height * .5;
                            bool q_clip = false;
                            // At the pad cap the bound is deliberately truncated
                            // (pad_limit): report those points, do not fail on
                            // them. In the game the near cut keeps w >= zn, far
                            // from the cap; see the header's derivation.
                            if (!covered(rect, v, qx, qy, &q_clip)) { ++outside_fp32; if (pad < pad_limit) fail = true; }
                        }
            }
        }
        if (outside) fail = true;
        if (fail) ++failures;
        std::printf("NEAR index=%u reason=%u plain_reason=%u behind=%u clipped=%u pad=%u rect=%d,%d,%d,%d viewport=%u,%u inside=%u outside=%u outside_fp32=%u capped=%u clipped_points=%u invisible=%u fail=%u\n",
                    c, unsigned(reason), unsigned(plain_reason), behind_corners, cut.clipped, pad, rect.left, rect.top, rect.right, rect.bottom, v.width, v.height, inside, outside, outside_fp32, unsigned(pad >= pad_limit), clipped, invisible, unsigned(fail));
    }
    std::printf("RESULT cases=%u failures=%u\n", cases, failures);
    return failures ? 1 : 0;
}
// --near-case rows(16) centre(3) half(3) viewport(4): derive with the near
// cut, as the locked-prefix source runs it.
int near_case_mode(int argc, char** argv) {
    if (argc < 2 + 16 + 3 + 3 + 4) { std::fprintf(stderr, "near-case arguments\n"); return 2; }
    float rows[16]; Box box{}; Viewport v{};
    int i = 2;
    for (auto& r : rows) r = float(std::strtod(argv[i++], nullptr));
    for (auto& c : box.centre) c = std::strtod(argv[i++], nullptr);
    for (auto& h : box.half) h = std::strtod(argv[i++], nullptr);
    v.x = unsigned(std::strtoul(argv[i++], nullptr, 10)); v.y = unsigned(std::strtoul(argv[i++], nullptr, 10));
    v.width = unsigned(std::strtoul(argv[i++], nullptr, 10)); v.height = unsigned(std::strtoul(argv[i++], nullptr, 10));
    const Region region = derive(rows, true, box, v, true, Rect{0, 0, 64, 64}, true);
    std::printf("REGION bound=%u reason=%u clipped=%u pad=%u rect=%d,%d,%d,%d f=%.9f\n", region.bound, unsigned(region.reason), region.clipped, region.pad,
                region.rect.left, region.rect.top, region.rect.right, region.rect.bottom, area_fraction(region.rect, v));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 5 && std::strcmp(argv[1], "--near") == 0)
        return near_mode(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)), unsigned(std::strtoul(argv[4], nullptr, 10)));
    if (argc >= 2 && std::strcmp(argv[1], "--near-case") == 0) return near_case_mode(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "--table") == 0) return table::run();
    if (argc >= 4 && std::strcmp(argv[1], "--prefix") == 0)
        return prefix_mode::run(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)));
    if (argc >= 5 && std::strcmp(argv[1], "--random") == 0)
        return random_mode(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)), unsigned(std::strtoul(argv[4], nullptr, 10)));
    if (argc >= 2 && std::strcmp(argv[1], "--case") == 0) return case_mode(argc, argv);
    std::fprintf(stderr, "usage: --random seed cases points | --case ... | --near seed cases points | --near-case ... | --table | --prefix seed cases\n");
    return 2;
}
