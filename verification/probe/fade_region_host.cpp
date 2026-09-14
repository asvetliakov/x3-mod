// Host driver for src/proxy/fade_region_math.h and locked_prefix_core.h
// (verification/analysis/test_fade_region.py). Random boxes, rows and
// viewports: every interior point projected on the CPU must land inside the
// derived rectangle; random triangle lists (step D) against a rasterising
// oracle; hand cases print the rectangle for exact comparison. No D3D, no
// Windows.
#include "../../src/proxy/fade_region_math.h"
#include "../../src/proxy/fade_region_core.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <chrono>
#include <random>
#include <string>
#include <utility>
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

// --prefix: the locked-prefix sentinel, scan, table and the
// resolve_locked_prefix binding (src/proxy/locked_prefix_core.h, steps B and
// D of the screen-emission notes) on host memory. Every case prints one
// PREFIX line; random cases prove the exact-prefix property (the positions a
// bound lookup hands out are bit-identical to the written prefix) with a
// garbage or sentinel tail.
namespace prefix_mode {
using namespace x3m::fade_region::prefix;
using x3m::fade_region::Box;
constexpr unsigned quad = 6;
std::vector<float> buffer(std::size_t vertices) { return std::vector<float>(vertices * stride / sizeof(float)); }
void put(std::vector<float>& b, std::size_t i, float x, float y, float z) { b[i * 6] = x; b[i * 6 + 1] = y; b[i * 6 + 2] = z; }
// Box of the leading count positions a lookup handed out (nullptr: zeros).
Box extent(const float* positions, std::uint32_t count) {
    Box box{};
    if (!positions || !count) return box;
    double lo[3], hi[3];
    for (unsigned a = 0; a < 3; ++a) lo[a] = hi[a] = positions[a];
    for (std::uint32_t i = 1; i < count; ++i) for (unsigned a = 0; a < 3; ++a) {
        const double v = positions[i * 3 + a];
        lo[a] = v < lo[a] ? v : lo[a]; hi[a] = v > hi[a] ? v : hi[a];
    }
    for (unsigned a = 0; a < 3; ++a) { box.centre[a] = (lo[a] + hi[a]) / 2; box.half[a] = (hi[a] - lo[a]) / 2; }
    return box;
}
struct Look { Lookup status = Lookup::Unknown; const float* positions = nullptr; std::uint64_t revision = 0; std::uint32_t scanned = 0; };
Look look(const Table& t, std::uintptr_t key, std::uint32_t count) {
    Look l; l.status = t.lookup(key, count, &l.positions, &l.revision, &l.scanned); return l;
}
void print(const char* label, const Look& l, std::uint32_t count, const Table& t, unsigned extra = 0) {
    const Box box = extent(l.status == Lookup::Bound ? l.positions : nullptr, count);
    std::printf("PREFIX %s status=%u name=%s box=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f scanned=%u revision=%llu used=%u evictions=%llu publications=%llu sentinel_bytes=%llu window_end=%llu extra=%u\n",
                label, unsigned(l.status), lookup_name(l.status), box.centre[0], box.centre[1], box.centre[2], box.half[0], box.half[1], box.half[2],
                l.scanned, (unsigned long long)l.revision, t.used(), (unsigned long long)t.evictions(), (unsigned long long)t.publications(),
                (unsigned long long)t.sentinel_bytes(), (unsigned long long)t.window_end_scans(), extra);
}
// Fake production binding for resolve_locked_prefix: one published record.
Table* env_table = nullptr;
bool env_prefix(std::uintptr_t wrapper, std::uint32_t vertex_count, const float** positions, std::uint32_t* scanned, std::uint64_t* revision, unsigned* refusal) noexcept {
    const Lookup l = env_table->lookup(wrapper, vertex_count, positions, revision, scanned);
    *refusal = unsigned(l);
    return l == Lookup::Bound;
}
int run(unsigned seed, unsigned cases) {
    Table t;
    // Vertex i at (i, -i, 2i): 200 vertices written by the game's writer,
    // i.e. after the Lock (the sentinel is written at begin_lock, the
    // prefix over it, the tail left alone).
    auto b = buffer(300);
    auto write_math = [&] { for (unsigned i = 0; i < 200; ++i) put(b, i, float(i), -float(i), 2.f * i); };
    print("math_unknown", look(t, 1, 96), 96, t);
    // An unmarked buffer's lock leaves no record and no sentinel; the draw marks it first.
    std::fill(b.begin(), b.end(), 0.f);
    print("unmarked_lock_ignored", (t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7), t.finish_lock(1, 7), look(t, 1, 96)), 96, t, unsigned(b[0] == 0.f));
    t.mark(1);
    print("marked_unknown", look(t, 1, 96), 96, t);
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7);
    std::uint32_t words[3]; std::memcpy(words, b.data() + 299 * 6, sizeof words);
    print("math_pending", look(t, 1, 96), 96, t, unsigned(words[0] == sentinel_word && words[2] == sentinel_word)); // whole window sentinelled
    write_math();
    const std::uint32_t scanned = t.finish_lock(1, 7);
    print("math_96", look(t, 1, 96), 96, t, scanned);       // exact: 200 written, scan stopped at the sentinel
    print("math_200", look(t, 1, 200), 200, t);
    print("math_1", look(t, 1, 1), 1, t);                  // a 1-vertex draw: its one vertex
    print("empty", look(t, 1, 0), 0, t);
    print("beyond", look(t, 1, 201), 201, t);
    print("beyond_max", look(t, 1, max_vertices + 1), 0, t);
    // Revision: a second lock makes the record pending (revision 2) and
    // sentinels exactly the previous prefix (200 vertices); a failed Unlock
    // invalidates; a non-DISCARD lock is invalid outright.
    const std::uint64_t before = t.sentinel_bytes();
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7);
    print("relock_pending", look(t, 1, 96), 96, t, unsigned(t.sentinel_bytes() - before));
    write_math(); t.finish_lock(1, 7); t.invalidate(1);
    print("unlock_failed", look(t, 1, 96), 96, t);
    t.begin_lock(1, false, nullptr, 0, 7); t.finish_lock(1, 7);
    print("non_discard", look(t, 1, 96), 96, t);
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7); write_math();
    print("thread_mismatch", (t.finish_lock(1, 8), look(t, 1, 96)), 96, t);
    t.begin_lock(1, true, b.data(), b.size() * sizeof(float), 7); write_math(); t.finish_lock(1, 7);
    print("relearned", look(t, 1, 200), 200, t);
    t.erase(1);
    print("erased", look(t, 1, 96), 96, t, t.allocated());
    // A nested lock (Lock while locked) invalidates the record through both
    // Unlocks; the next fresh lock publishes again.
    t.mark(3);
    t.begin_lock(3, true, b.data(), b.size() * sizeof(float), 7); t.begin_lock(3, true, b.data(), b.size() * sizeof(float), 7);
    t.finish_lock(3, 7);
    print("nested_first_unlock", look(t, 3, 96), 96, t);
    t.finish_lock(3, 7);
    print("nested_invalid", look(t, 3, 96), 96, t);
    t.begin_lock(3, true, b.data(), b.size() * sizeof(float), 7); write_math(); t.finish_lock(3, 7);
    print("nested_relearned", look(t, 3, 96), 96, t);
    t.erase(3);
    // Tails: 100 valid vertices in [-1, 1]; the writer then either leaves
    // the sentinel (as the game does) or overwrites the tail with what a
    // recycled DISCARD window could hold.
    auto g = buffer(300);
    auto prefix = [&] { for (unsigned i = 0; i < 100; ++i) { const float v = i % 2 ? 1.f : -1.f; put(g, i, v, v, v); } };
    auto tail = [&](float value) { for (unsigned i = 100; i < 300; ++i) put(g, i, value, value, value); };
    const auto publish = [&](std::uintptr_t key, auto&& writer) { t.mark(key); t.begin_lock(key, true, g.data(), g.size() * sizeof(float), 7); writer(); return t.finish_lock(key, 7); };
    std::uint32_t n = publish(2, [&] { prefix(); });
    print("tail_sentinel_100", look(t, 2, 100), 100, t, n);                     // exact count: 100
    print("tail_sentinel_101", look(t, 2, 101), 101, t);                        // beyond
    n = publish(2, [&] { prefix(); tail(std::numeric_limits<float>::quiet_NaN()); });
    print("tail_nan_100", look(t, 2, 100), 100, t, n);                          // the prefix is clean: bound
    print("tail_nan_101", look(t, 2, 101), 101, t);                             // vertex 100 is NaN: refused
    n = publish(2, [&] { prefix(); tail(std::numeric_limits<float>::infinity()); });
    print("tail_inf_100", look(t, 2, 100), 100, t, n);
    print("tail_inf_106", look(t, 2, 106), 106, t);
    n = publish(2, [&] { prefix(); tail(1e30f); });
    print("tail_absurd_100", look(t, 2, 100), 100, t, n);                       // beyond world_limit only past the prefix
    print("tail_absurd_106", look(t, 2, 106), 106, t);
    n = publish(2, [&] { prefix(); tail(world_limit); });
    print("tail_limit_106", look(t, 2, 106), 106, t, n);                        // exactly the limit: finite
    n = publish(2, [&] { prefix(); tail(1e6f); });
    print("tail_huge_100", look(t, 2, 100), 100, t, n);                         // finite garbage: not in the 100-vertex prefix
    print("tail_huge_106", look(t, 2, 106), 106, t);                            // but in a 106-vertex draw
    n = publish(2, [&] { prefix(); tail(0.f); });
    print("tail_zero_100", look(t, 2, 100), 100, t, n);                         // the run-15 origin tail: scan to the window end, exact draw
    // A NaN inside the valid prefix refuses every draw that covers it.
    n = publish(2, [&] { prefix(); put(g, 5, std::numeric_limits<float>::quiet_NaN(), 0, 0); });
    print("prefix_nan_5", look(t, 2, 5), 5, t, n);
    print("prefix_nan_6", look(t, 2, 6), 6, t);
    print("prefix_nan_96", look(t, 2, 96), 96, t);
    // A real vertex that equals the sentinel ends the scan early: a draw past it is beyond.
    n = publish(2, [&] { prefix(); std::uint32_t s[3] = {sentinel_word, sentinel_word, sentinel_word}; std::memcpy(g.data() + 50 * 6, s, sizeof s); });
    print("sentinel_vertex_50", look(t, 2, 50), 50, t, n);
    print("sentinel_vertex_51", look(t, 2, 51), 51, t);
    // The sentinel extent follows the previous scan: after the 300-vertex
    // window-end scan above the next lock sentinels 300 vertices, after an
    // exact 100 it sentinels 100.
    { const std::uint64_t s0 = t.sentinel_bytes(); n = publish(2, [&] { prefix(); }); const std::uint64_t s1 = t.sentinel_bytes();
      print("sentinel_extent_after_50", look(t, 2, 100), 100, t, unsigned(s1 - s0)); // previous scan: 50 vertices -> 1200 bytes
      const std::uint64_t s2 = t.sentinel_bytes(); publish(2, [&] { prefix(); });
      print("sentinel_extent_after_100", look(t, 2, 100), 100, t, unsigned(t.sentinel_bytes() - s2)); }
    // Partial trailing vertex: 10 vertices plus 7 bytes scan as 10; a window
    // past max_vertices is capped.
    std::vector<float> positions(storage_floats);
    Scan partial{};
    std::fill(g.begin(), g.end(), 0.f);
    print("length_partial", Look{Lookup(scan(g.data(), 10 * stride + 7, positions.data(), &partial)), nullptr, partial.bad_from, partial.vertices}, 0, t, unsigned(partial.window_end));
    Scan capped{};
    auto big = buffer(max_vertices + 10);
    print("length_capped", Look{Lookup(scan(big.data(), big.size() * sizeof(float), positions.data(), &capped)), nullptr, capped.bad_from, capped.vertices}, 0, t, unsigned(capped.window_end));
    // Eviction: capacity + 1 distinct keys; the oldest (key 100) goes; the
    // storage pool stays at capacity blocks.
    Table e;
    for (unsigned k = 0; k <= Table::capacity; ++k) { e.mark(100 + k); e.begin_lock(100 + k, true, b.data(), b.size() * sizeof(float), 7); write_math(); e.finish_lock(100 + k, 7); }
    print("evicted_oldest", look(e, 100, 96), 96, e, e.allocated());
    print("evicted_kept", look(e, 101, 96), 96, e);
    e.clear();
    print("cleared", look(e, 101, 96), 96, e, e.allocated());
    // resolve_locked_prefix through the Environment binding.
    Table r; r.mark(0xd000); r.begin_lock(0xd000, true, b.data(), b.size() * sizeof(float), 7); write_math(); r.finish_lock(0xd000, 7); env_table = &r;
    const x3m::fade_region::Environment env{&table::read_span, &table::scope, &table::content, &env_prefix};
    const x3m::fade_region::Environment no_prefix{&table::read_span, &table::scope, &table::content, nullptr};
    auto res = [&](const char* label, const x3m::fade_region::Result& out) {
        const Box box = extent(out.positions, out.vertex_count);
        std::printf("RESOLVE %s status=%u name=%s source=%u bound=%u refusal=%u scanned=%u revision=%llu positions=%u box=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n", label, unsigned(out.status),
                    x3m::fade_region::status_name(out.status), unsigned(out.source), out.status == x3m::fade_region::Status::Bound, out.prefix_refusal, out.scanned,
                    (unsigned long long)out.vb_revision, out.positions != nullptr, box.centre[0], box.centre[1], box.centre[2], box.half[0], box.half[1], box.half[2]);
    };
    res("bound", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 200, env));
    res("no_vb", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{0, 0, 0, 0}, 200, env));
    res("zero_count", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 0, env));
    res("no_binding", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 200, no_prefix));
    res("unknown_buffer", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd001, 0}, 200, env));
    res("beyond", x3m::fade_region::resolve_locked_prefix(x3m::fade_region::Query{5, 0, 0xd000, 0}, 201, env));
    // Random exact-prefix cases: N quads (6 vertices each) inside a random
    // box written after the lock; even cases leave the sentinel tail, odd
    // cases overwrite the whole window with finite garbage first (a recycled
    // DISCARD window). The bound positions must equal the written prefix bit
    // for bit and the box they span the true extent; the scan count is
    // exact with the sentinel and the window with garbage.
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit(0, 1);
    unsigned failures = 0, exact = 0, garbage = 0;
    auto v = buffer(max_vertices);
    for (unsigned c = 0; c < cases; ++c) {
        const unsigned quads = 1 + unsigned(unit(rng) * (max_vertices / quad));
        const unsigned count = quads * quad;
        double centre[3], half[3];
        for (unsigned a = 0; a < 3; ++a) { centre[a] = unit(rng) * 2000 - 1000; half[a] = unit(rng) * 100; }
        const float tail = float(unit(rng) * 4000 - 2000);
        Table s; s.mark(9); s.begin_lock(9, true, v.data(), v.size() * sizeof(float), 1);
        const bool recycled = c % 2 == 1;
        if (recycled) for (unsigned i = 0; i < max_vertices; ++i) put(v, i, tail, -tail, tail * .5f);
        float lo[3] = {3e38f, 3e38f, 3e38f}, hi[3] = {-3e38f, -3e38f, -3e38f};
        for (unsigned i = 0; i < count; ++i) {
            float p[3];
            for (unsigned a = 0; a < 3; ++a) { p[a] = float(centre[a] + (unit(rng) * 2 - 1) * half[a]); lo[a] = p[a] < lo[a] ? p[a] : lo[a]; hi[a] = p[a] > hi[a] ? p[a] : hi[a]; }
            put(v, i, p[0], p[1], p[2]);
        }
        const std::uint32_t n = s.finish_lock(9, 1);
        const Look l = look(s, 9, count);
        bool ok = l.status == Lookup::Bound && l.positions && n == (recycled ? max_vertices : count) && l.scanned == n;
        for (unsigned i = 0; i < count && ok; ++i)
            for (unsigned a = 0; a < 3; ++a) ok = ok && std::memcmp(&l.positions[i * 3 + a], &v[i * 6 + a], sizeof(float)) == 0;
        if (ok) {
            const Box box = extent(l.positions, count);
            for (unsigned a = 0; a < 3; ++a) ok = ok && box.centre[a] - box.half[a] == double(lo[a]) && box.centre[a] + box.half[a] == double(hi[a]);
        }
        if (ok && look(s, 9, count + 1).status != (recycled ? Lookup::Bound : Lookup::Beyond)) ok = false; // one past the prefix
        if (!ok) ++failures; else if (recycled) ++garbage; else ++exact;
    }
    // Scan cost on this host: an exact 1056-vertex prefix behind the
    // sentinel, and the whole 6144-vertex window (no sentinel met).
    volatile float sink = 0;
    auto time_scan = [&](std::uint32_t written, bool sentinel_tail) {
        Table s; s.mark(9); s.begin_lock(9, true, v.data(), v.size() * sizeof(float), 1);
        if (!sentinel_tail) for (unsigned i = 0; i < max_vertices; ++i) put(v, i, 1, 2, 3);
        for (unsigned i = 0; i < written; ++i) put(v, i, float(i), 1, 2);
        Scan timing{};
        const auto begin = std::chrono::steady_clock::now();
        constexpr unsigned iterations = 2000;
        for (unsigned i = 0; i < iterations; ++i) { v[i % 6] = float(i); scan(v.data(), v.size() * sizeof(float), positions.data(), &timing); sink = sink + positions[3]; }
        const double ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count()) / iterations;
        s.finish_lock(9, 1);
        return std::pair<double, std::uint32_t>(ns, timing.vertices);
    };
    const auto exact_scan = time_scan(1056, true);
    const auto window_scan = time_scan(1056, false);
    std::printf("PREFIX_RANDOM cases=%u failures=%u exact=%u garbage=%u scan_ns_1056=%.0f vertices_1056=%u scan_ns_window=%.0f vertices_window=%u\n",
                cases, failures, exact, garbage, exact_scan.first, exact_scan.second, window_scan.first, window_scan.second);
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

// --hull seed cases triangles: step D (screen-emission-bullet-bound.md).
// Random triangle lists through perspective-like rows whose near plane cuts
// through, before or behind them, against an independent oracle: each
// triangle's clip coordinates are evaluated the way the GPU does (fp32
// dp4), the polygon is clipped against z >= 0 (Sutherland-Hodgman, no
// shared code with project_prefix), projected, and rasterised with pixel
// centres; every covered pixel must lie inside the derived rectangle
// (outside == 0). A BehindNear list must cover no pixel; the derivation's
// behind count never exceeds the oracle's (its cut sits eps behind the
// plane); the rectangle lies inside the near-clipped rectangle of the list's
// own AABB (the step-B route) unless it is the off-screen 1x1 fallback.
namespace hull_mode {
struct Point { double x, y; };
// Screen-space polygon of one triangle's visible part, from fp32 clip values.
std::vector<Point> visible_polygon(const float rows[16], const float* tri, const Viewport& v, unsigned* behind) {
    struct Clip { double c[4]; };
    std::vector<Clip> poly;
    for (unsigned k = 0; k < 3; ++k) {
        Clip q{};
        for (unsigned r = 0; r < 4; ++r) {
            // fp32 dp4 with left-to-right accumulation, as a scalar shader would.
            float acc = rows[4 * r] * tri[3 * k];
            acc += rows[4 * r + 1] * tri[3 * k + 1];
            acc += rows[4 * r + 2] * tri[3 * k + 2];
            acc += rows[4 * r + 3];
            q.c[r] = acc;
        }
        if (q.c[2] < 0) ++*behind;
        poly.push_back(q);
    }
    // Clip against z >= 0.
    std::vector<Clip> out;
    for (unsigned i = 0; i < poly.size(); ++i) {
        const Clip& a = poly[i]; const Clip& b = poly[(i + 1) % poly.size()];
        const bool ina = a.c[2] >= 0, inb = b.c[2] >= 0;
        if (ina) out.push_back(a);
        if (ina != inb) {
            const double t = a.c[2] / (a.c[2] - b.c[2]);
            Clip m{};
            for (unsigned r = 0; r < 4; ++r) m.c[r] = a.c[r] + t * (b.c[r] - a.c[r]);
            out.push_back(m);
        }
    }
    std::vector<Point> screen;
    for (const Clip& q : out) {
        if (!(q.c[3] > 0)) return {}; // not a perspective row set here: the derivation refuses NonPositiveW
        screen.push_back({v.x + (q.c[0] / q.c[3] + 1) * v.width * .5, v.y + (1 - q.c[1] / q.c[3]) * v.height * .5});
    }
    return screen;
}
bool inside(const std::vector<Point>& poly, double px, double py) {
    // Convex polygon, either winding: the point is inside when it lies on
    // the same side of every edge (or on an edge).
    int sign = 0;
    for (unsigned i = 0; i < poly.size(); ++i) {
        const Point& a = poly[i]; const Point& b = poly[(i + 1) % poly.size()];
        const double cross = (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
        if (cross == 0) continue;
        const int s = cross > 0 ? 1 : -1;
        if (sign == 0) sign = s; else if (s != sign) return false;
    }
    return true;
}
struct Footprint { std::uint64_t covered = 0, outside = 0; std::int32_t min_x = 0, min_y = 0, max_x = -1, max_y = -1; };
// Pixels whose centre lies inside the polygon (full rasterisation up to a
// budget, then a random sample plus the pixels around each vertex).
void rasterise(const std::vector<Point>& poly, const Viewport& v, const Rect& rect, std::mt19937_64& rng, Footprint* fp) {
    if (poly.size() < 3) return;
    double lo_x = poly[0].x, hi_x = poly[0].x, lo_y = poly[0].y, hi_y = poly[0].y;
    for (const Point& p : poly) { lo_x = std::min(lo_x, p.x); hi_x = std::max(hi_x, p.x); lo_y = std::min(lo_y, p.y); hi_y = std::max(hi_y, p.y); }
    const double vx0 = v.x, vy0 = v.y, vx1 = double(v.x) + v.width, vy1 = double(v.y) + v.height;
    const std::int32_t x0 = std::int32_t(std::floor(std::max(lo_x, vx0))), x1 = std::int32_t(std::ceil(std::min(hi_x, vx1)));
    const std::int32_t y0 = std::int32_t(std::floor(std::max(lo_y, vy0))), y1 = std::int32_t(std::ceil(std::min(hi_y, vy1)));
    if (x1 <= x0 || y1 <= y0) return;
    auto test = [&](std::int32_t x, std::int32_t y) {
        if (x < std::int32_t(vx0) || y < std::int32_t(vy0) || x >= std::int32_t(vx1) || y >= std::int32_t(vy1)) return;
        if (!inside(poly, x + .5, y + .5)) return;
        ++fp->covered;
        if (fp->max_x < fp->min_x) { fp->min_x = fp->max_x = x; fp->min_y = fp->max_y = y; }
        fp->min_x = std::min(fp->min_x, x); fp->max_x = std::max(fp->max_x, x); fp->min_y = std::min(fp->min_y, y); fp->max_y = std::max(fp->max_y, y);
        if (!contains(rect, x, y)) ++fp->outside;
    };
    const std::uint64_t pixels = std::uint64_t(x1 - x0) * std::uint64_t(y1 - y0);
    if (pixels <= 40000) { for (std::int32_t y = y0; y < y1; ++y) for (std::int32_t x = x0; x < x1; ++x) test(x, y); return; }
    std::uniform_int_distribution<std::int32_t> dx(x0, x1 - 1), dy(y0, y1 - 1);
    for (unsigned n = 0; n < 4000; ++n) test(dx(rng), dy(rng));
    for (const Point& p : poly) for (int ox = -2; ox <= 2; ++ox) for (int oy = -2; oy <= 2; ++oy) test(std::int32_t(std::floor(p.x)) + ox, std::int32_t(std::floor(p.y)) + oy);
}
int run(unsigned seed, unsigned cases, unsigned triangles_max) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> unit(0, 1);
    unsigned failures = 0, capped = 0;
    for (unsigned c = 0; c < cases; ++c) {
        Viewport v{0, 0, 16 + unsigned(unit(rng) * 2032), 16 + unsigned(unit(rng) * 1064)};
        if (c % 5 == 4) { v.x = unsigned(unit(rng) * 40); v.y = unsigned(unit(rng) * 40); }
        // x' = s x + tx, y' = s y + ty, z' = k (z + d - zn), w = z + d: near plane at w = zn.
        const double s = 0.25 + unit(rng) * 8, d = unit(rng) * 10 - 5, zn = 0.01 + unit(rng) * 3, k = 0.05 + unit(rng);
        const double world = (c % 3 == 0) ? std::pow(10.0, 3 + unit(rng) * 2) : 0.0; // bullet-scale cancellation
        const double tx = unit(rng) * 4 - 2, ty = unit(rng) * 4 - 2;
        const float rows[16] = {float(s), 0, 0, float(tx - s * world), 0, float(s), 0, float(ty - s * world),
                                0, 0, float(k), float(k * (d - zn) - k * world), 0, 0, 1, float(d - world)};
        const unsigned triangles = 1 + unsigned(unit(rng) * triangles_max);
        std::vector<float> positions(triangles * 9);
        // Geometry kinds: scattered small triangles, one long beam along a
        // diagonal, a batch entirely behind, a batch hugging the near plane.
        const unsigned kind = c % 4;
        double centre[3] = {unit(rng) * 6 - 3, unit(rng) * 6 - 3, unit(rng) * 10 - 4};
        double half[3] = {unit(rng) * 3, unit(rng) * 3, unit(rng) * 4};
        if (kind == 2) { centre[2] = -d - zn - 5 - unit(rng) * 5; half[2] = std::min(half[2], 4.0); }
        if (kind == 3) { centre[2] = -d + zn; half[2] = 0.02 + unit(rng) * 0.2; }
        for (unsigned t = 0; t < triangles; ++t) {
            double base[3];
            const double along = unit(rng);
            for (unsigned a = 0; a < 3; ++a) base[a] = kind == 1 ? centre[a] - half[a] + 2 * half[a] * along : centre[a] + (unit(rng) * 2 - 1) * half[a];
            const double size = kind == 1 ? 0.05 + unit(rng) * 0.2 : 0.02 + unit(rng) * 1.5;
            for (unsigned k2 = 0; k2 < 3; ++k2) for (unsigned a = 0; a < 3; ++a) positions[t * 9 + k2 * 3 + a] = float(base[a] + world * (a < 3) + (unit(rng) * 2 - 1) * size);
        }
        Rect rect{};
        PrefixHull hull{};
        const Reason reason = project_prefix(rows, positions.data(), triangles * 3, v, &rect, &hull);
        Footprint fp{};
        unsigned behind = 0;
        bool degenerate_w = false;
        for (unsigned t = 0; t < triangles; ++t) {
            unsigned b = 0;
            const auto poly = visible_polygon(rows, positions.data() + t * 9, v, &b);
            behind += b;
            if (b < 3 && poly.empty()) degenerate_w = true;
            rasterise(poly, v, reason == Reason::Bound ? rect : Rect{0, 0, 0, 0}, rng, &fp);
        }
        bool fail = false;
        if (hull.behind > behind) fail = true;
        if (reason == Reason::BehindNear && fp.covered) fail = true;
        if (reason != Reason::Bound && reason != Reason::BehindNear && !degenerate_w) fail = true;
        if (reason == Reason::Bound && fp.outside) fail = true;
        // The step-B box of the same vertices contains the hull rectangle.
        Rect box_rect{}; NearClip cut{true, 0};
        std::uint64_t aabb_px = 0;
        if (reason == Reason::Bound) {
            const Reason box_reason = project_box(rows, hull.aabb, v, &box_rect, &cut);
            if (box_reason != Reason::Bound) fail = true;
            else {
                aabb_px = area(box_rect);
                const bool fallback = area(rect) == 1 && rect.left == std::int32_t(v.x) && rect.top == std::int32_t(v.y) && fp.covered == 0;
                if (!fallback && (rect.left < box_rect.left || rect.top < box_rect.top || rect.right > box_rect.right || rect.bottom > box_rect.bottom)) fail = true;
            }
        }
        if (fail) ++failures;
        // As --near: cases at the pad cap are reported, not hidden (the
        // exact-arithmetic oracle cannot see the truncation; --near's fp32
        // perturbation does).
        const bool at_cap = reason == Reason::Bound && hull.pad >= pad_limit;
        capped += at_cap;
        std::printf("HULL index=%u kind=%u triangles=%u reason=%u behind=%u clipped=%u pad=%u capped=%u rect=%d,%d,%d,%d hull_px=%llu aabb_px=%llu viewport=%u,%u covered=%llu outside=%llu footprint=%d,%d,%d,%d fail=%u\n",
                    c, kind, triangles, unsigned(reason), behind, hull.behind, hull.pad, unsigned(at_cap), rect.left, rect.top, rect.right, rect.bottom,
                    (unsigned long long)(reason == Reason::Bound ? area(rect) : 0), (unsigned long long)aabb_px, v.width, v.height,
                    (unsigned long long)fp.covered, (unsigned long long)fp.outside, fp.min_x, fp.min_y, fp.max_x + 1, fp.max_y + 1, unsigned(fail));
    }
    std::printf("RESULT cases=%u failures=%u capped=%u\n", cases, failures, capped);
    return failures ? 1 : 0;
}
// --hull-case rows(16) viewport(4) count xyz...: the prefix derivation as
// the locked-prefix source runs it, plus the oracle footprint and the
// near-clipped AABB rectangle of the same vertices.
int case_mode(int argc, char** argv) {
    if (argc < 2 + 16 + 4 + 1) { std::fprintf(stderr, "hull-case arguments\n"); return 2; }
    float rows[16]; Viewport v{};
    int i = 2;
    for (auto& r : rows) r = float(std::strtod(argv[i++], nullptr));
    v.x = unsigned(std::strtoul(argv[i++], nullptr, 10)); v.y = unsigned(std::strtoul(argv[i++], nullptr, 10));
    v.width = unsigned(std::strtoul(argv[i++], nullptr, 10)); v.height = unsigned(std::strtoul(argv[i++], nullptr, 10));
    const unsigned count = unsigned(std::strtoul(argv[i++], nullptr, 10));
    if (argc < i + int(count) * 3) { std::fprintf(stderr, "hull-case positions\n"); return 2; }
    std::vector<float> positions(count * 3);
    for (auto& p : positions) p = float(std::strtod(argv[i++], nullptr));
    PrefixHull hull{};
    const Region region = derive_prefix(rows, positions.data(), count, v, true, Rect{0, 0, 64, 64}, &hull);
    std::mt19937_64 rng(1);
    Footprint fp{};
    for (unsigned t = 0; t + 3 <= count; t += 3) {
        unsigned b = 0;
        const auto poly = visible_polygon(rows, positions.data() + t * 3, v, &b);
        rasterise(poly, v, region.bound ? region.rect : Rect{0, 0, 0, 0}, rng, &fp);
    }
    Rect box_rect{}; NearClip cut{true, 0};
    const Reason box_reason = count && count % 3 == 0 ? project_box(rows, hull.aabb, v, &box_rect, &cut) : Reason::BoundUnknown;
    std::printf("HULL_CASE bound=%u reason=%u clipped=%u pad=%u rect=%d,%d,%d,%d f=%.9f hull_px=%llu aabb_reason=%u aabb_rect=%d,%d,%d,%d aabb_px=%llu covered=%llu outside=%llu footprint=%d,%d,%d,%d\n",
                region.bound, unsigned(region.reason), region.clipped, region.pad, region.rect.left, region.rect.top, region.rect.right, region.rect.bottom,
                area_fraction(region.rect, v), (unsigned long long)(region.bound ? area(region.rect) : 0), unsigned(box_reason), box_rect.left, box_rect.top, box_rect.right, box_rect.bottom,
                (unsigned long long)(box_reason == Reason::Bound ? area(box_rect) : 0), (unsigned long long)fp.covered, (unsigned long long)fp.outside,
                fp.min_x, fp.min_y, fp.max_x + 1, fp.max_y + 1);
    return 0;
}
} // namespace hull_mode

} // namespace

int main(int argc, char** argv) {
    if (argc >= 5 && std::strcmp(argv[1], "--hull") == 0)
        return hull_mode::run(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)), unsigned(std::strtoul(argv[4], nullptr, 10)));
    if (argc >= 2 && std::strcmp(argv[1], "--hull-case") == 0) return hull_mode::case_mode(argc, argv);
    if (argc >= 5 && std::strcmp(argv[1], "--near") == 0)
        return near_mode(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)), unsigned(std::strtoul(argv[4], nullptr, 10)));
    if (argc >= 2 && std::strcmp(argv[1], "--near-case") == 0) return near_case_mode(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "--table") == 0) return table::run();
    if (argc >= 4 && std::strcmp(argv[1], "--prefix") == 0)
        return prefix_mode::run(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)));
    if (argc >= 5 && std::strcmp(argv[1], "--random") == 0)
        return random_mode(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)), unsigned(std::strtoul(argv[4], nullptr, 10)));
    if (argc >= 2 && std::strcmp(argv[1], "--case") == 0) return case_mode(argc, argv);
    std::fprintf(stderr, "usage: --random seed cases points | --case ... | --near seed cases points | --near-case ... | --hull seed cases triangles | --hull-case ... | --table | --prefix seed cases\n");
    return 2;
}
