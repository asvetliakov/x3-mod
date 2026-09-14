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
#include <random>
#include <string>

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
    std::printf("REGION bound=%u reason=%u rect=%d,%d,%d,%d f=%.9f\n", region.bound, unsigned(region.reason),
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
    const Environment env{&read_span, &scope, &content};
    const Query query{101, 202, vb, ib};
    BoundTable t;
    print("no_table", t.resolve(query, env), t);
    if (!t.reserve()) return 3;
    scope_depth = 0; reset_memory(); scope_depth = 0;
    print("no_scope", t.resolve(query, env), t);
    reset_memory();
    print("miss_learn", t.resolve(query, env), t);   // reads: head, aabb, back, record0, record1
    print("hit", t.resolve(query, env), t);           // no reads
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
    print("poison_revision", t.resolve(query, env), t);
    contents[0].revision = 7;
    print("poisoned_stays", t.resolve(query, env), t);
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
} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--table") == 0) return table::run();
    if (argc >= 5 && std::strcmp(argv[1], "--random") == 0)
        return random_mode(unsigned(std::strtoul(argv[2], nullptr, 10)), unsigned(std::strtoul(argv[3], nullptr, 10)), unsigned(std::strtoul(argv[4], nullptr, 10)));
    if (argc >= 2 && std::strcmp(argv[1], "--case") == 0) return case_mode(argc, argv);
    std::fprintf(stderr, "usage: --random seed cases points | --case ...\n");
    return 2;
}
