// X3 CPU fixture of the scene-graph census row (src/proxy/scene_graph_census.cpp, its core
// scene_graph_census_core.h and the real engine_memory reader): a synthetic render manager with
// the four hash-table headers, a body table, and an unattached list of nodes allocated one by one
// on the process heap (0x270 bytes each, like the engine's malloc) and linked in a shuffled order.
// Checks the row: counts and their refusal, the list length, the sampled histogram with body names,
// a cycle, an unreadable tail, a foreign tail, the time budget and the 300,000-node bound, a missing
// manager, the insert-caller fields, LastError preserved. Measures the walk cost per row (raw pointer
// chase, next-only, stride 64, stride 1) at 200k nodes and the insert-caller capture per insert.
// Diagnostic timings only; not game FPS. Never launches the game.
#include "../../src/proxy/scene_graph_census.h"
#include "../../src/proxy/scene_graph_census_core.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/proxy/object_lifetime.h"
#include "../../src/proxy/address_space.h"
#include <windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace core = x3m::scene_graph_census::core;
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", what);
    }
}
char row_[8192];
bool stub_installed_ = true, callers_on_ = false;
std::uint32_t stub_live_ = 0;
core::CallerTable stub_callers_{};
}
// Stubs of the production dependencies the census links against.
namespace x3m {
void log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(row_, sizeof row_, format, args);
    va_end(args);
}
namespace object_lifetime {
Stats stats() {
    Stats s{};
    s.installed = stub_installed_;
    s.live = stub_live_;
    return s;
}
void set_insert_callers(bool on) {
    callers_on_ = on;
}
bool take_insert_callers(scene_graph_census::core::CallerTable* out) {
    if (!out) return false;
    *out = stub_callers_;
    stub_callers_ = {};
    return true;
}
}
}

namespace {
std::uint32_t g_manager = 0, g_vm = 0; // the synthetic image globals
constexpr unsigned max_nodes = 310000;
std::vector<unsigned char*> nodes_;       // heap blocks in allocation order
std::vector<unsigned> order_;             // link order (shuffled)
unsigned char* manager_ = nullptr;        // R, 0x1000 bytes
unsigned char* noaccess_ = nullptr;       // one PAGE_NOACCESS page
std::uint32_t u32(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
void put(void* at, std::uint32_t v) {
    std::memcpy(at, &v, 4);
}
const int mix_[10] = {5, 5, 5, 7, 7, 9, 5, 7, 5, 11};
// Links n nodes onto R+0x28 (head, 0, tailpred) in the shuffled order or in allocation
// order (the engine's AddTail on creation), model ids by position.
bool shuffled_ = true;
unsigned char* at_(unsigned i) {
    return nodes_[shuffled_ ? order_[i] : i];
}
void link(unsigned n, bool shuffled = true) {
    shuffled_ = shuffled;
    unsigned char* r = manager_;
    put(r + 0x28, n ? u32(at_(0)) : u32(r + 0x2c));
    put(r + 0x2c, 0);
    put(r + 0x30, n ? u32(at_(n - 1)) : u32(r + 0x28));
    std::uint32_t prev = u32(r + 0x28);
    for (unsigned i = 0; i < n; ++i) {
        unsigned char* node = at_(i);
        put(node, i + 1 < n ? u32(at_(i + 1)) : u32(r + 0x2c));
        put(node + 4, prev);
        put(node + 0x140, std::uint32_t(mix_[i % 10]));
        prev = u32(node);
    }
}
std::uint64_t qpc() {
    LARGE_INTEGER v{};
    QueryPerformanceCounter(&v);
    return std::uint64_t(v.QuadPart);
}
double us_since(std::uint64_t t0) {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return double(qpc() - t0) * 1e6 / double(f.QuadPart);
}
// Field value of the last row ("-" or digits); empty when absent.
std::string field(const char* name) {
    char key[64];
    std::snprintf(key, sizeof key, " %s=", name);
    const char* at = std::strstr(row_, key);
    if (!at) return {};
    at += std::strlen(key);
    const char* end = std::strchr(at, ' ');
    return end ? std::string(at, end) : std::string(at);
}
unsigned long long num(const char* name) {
    const std::string v = field(name);
    return v.empty() || v == "-" ? ~0ull : std::strtoull(v.c_str(), nullptr, 10);
}
unsigned long long frame_ = 300;
void report() {
    x3m::engine_memory::next_frame();
    row_[0] = 0;
    x3m::scene_graph_census::report(1, frame_);
    frame_ += 300;
}
}

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    // Layout: manager, tables, VM, body table.
    manager_ = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    noaccess_ = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS));
    if (!manager_ || !noaccess_) return 2;
    unsigned char* tables = manager_ + 0x1000;   // four 16-byte headers
    unsigned char* vm = manager_ + 0x2000;
    unsigned char* slots = manager_ + 0x3000;    // body slots 0..15
    char* names = reinterpret_cast<char*>(manager_ + 0x4000);
    put(manager_ + 0x0c, u32(tables + 0x00));
    put(manager_ + 0x10, u32(tables + 0x10));
    put(manager_ + 0x84, u32(tables + 0x20));
    put(vm + 0x00, u32(tables + 0x30));
    const std::uint32_t counts[4] = {123456, 17, 950, 4321};
    for (unsigned t = 0; t < 4; ++t) {
        put(tables + t * 0x10 + 4, 4096);
        put(tables + t * 0x10 + 0xc, counts[t]);
    }
    put(manager_ + 0xb4, 11000);
    put(manager_ + 0xb8, 16);
    put(manager_ + 0xbc, u32(slots));
    std::strcpy(names, "objects\\ships\\argon_m5");
    std::strcpy(names + 0x100, "objects\\effects\\bolt one");
    put(slots + 5 * 0x1c + 0x0c, u32(names));
    put(slots + 7 * 0x1c + 0x0c, u32(names + 0x100));
    g_manager = u32(manager_);
    g_vm = u32(vm);
    x3m::scene_graph_census::fixture_globals(reinterpret_cast<std::uintptr_t>(&g_manager),
                                              reinterpret_cast<std::uintptr_t>(&g_vm));
    // Nodes: one heap block each, like 0x00486d10's malloc(0x270); linked in a shuffled order.
    nodes_.reserve(max_nodes);
    for (unsigned i = 0; i < max_nodes; ++i) {
        auto* p = static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x270));
        if (!p) {
            std::printf("FAIL heap allocation at node %u\n", i);
            return 2;
        }
        nodes_.push_back(p);
    }
    order_.resize(max_nodes);
    for (unsigned i = 0; i < max_nodes; ++i) order_[i] = i;
    std::uint32_t seed = 0x9e3779b9u;
    for (unsigned i = max_nodes - 1; i > 0; --i) { // Fisher-Yates with xorshift32
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        std::swap(order_[i], order_[seed % (i + 1)]);
    }

    x3m::scene_graph_census::report(1, 0);
    check(row_[0] == 0, "not initialized: no row");
    x3m::scene_graph_census::initialize(true);
    check(callers_on_, "initialize(true) arms the insert-caller capture");

    // 1. 1000 nodes: counts, length, sampled histogram, body names, callers, registry_live.
    link(1000);
    stub_live_ = 123450;
    core::caller_add(stub_callers_, 0x00486d7d, 0x00401234);
    core::caller_add(stub_callers_, 0x0047a6b2, 0x00409abc);
    core::caller_add(stub_callers_, 0x0047a6b2, 0x00409abc);
    SetLastError(0x4d2);
    report();
    check(GetLastError() == 0x4d2, "LastError preserved across the row");
    check(num("engine_nodes") == 123456 && num("scenes") == 17 && num("cuts") == 950 && num("cut_buckets") == 4096 &&
              num("tasks") == 4321,
          "row: the four counts and the cut buckets");
    check(num("unattached") == 1000 && num("truncated") == 0 && num("cycle") == 0 && num("bounded") == 0 &&
              num("capped") == 0 && num("sampled") == 1 && num("sampled_nodes") == 1000 && num("distinct") == 4 &&
              num("other") == 0,
          "row: 1000 nodes, every model id read, four ids");
    // ids by position i % 10: 5 x500, 7 x300, 9 x100, 11 x100 (ties: the lower id first)
    check(field("b0") == "objects\\ships\\argon_m5:500" && field("b1") == "objects\\effects\\bolt?one:300" &&
              field("b2") == "v\\00009:100" && field("b3") == "v\\00011:100" && field("b4").empty(),
          "row: histogram by body name, counts, ties by id");
    check(num("registry_live") == 123450 && num("inserts") == 3 && num("insert_pairs") == 2 && num("insert_dropped") == 0 &&
              field("i0") == "0047a6b2/00409abc:2" && field("i1") == "00486d7d/00401234:1",
          "row: registry_live and the insert callers, sorted");
    report();
    check(num("inserts") == 0 && field("i0").empty(), "row: insert callers reset per row");
    stub_installed_ = false;
    report();
    check(field("registry_live") == "-", "row: registry_live - when the observer is not installed");
    stub_installed_ = true;

    // 2. A cycle: walking newest first, the 301st node links back to the tail.
    link(1000);
    put(at_(300) + 4, u32(at_(999)));
    report();
    check(num("cycle") == 1 && num("truncated") == 0 && num("unattached") >= 700 && num("unattached") <= 4000,
          "row: cycle=1 within 4n");

    // 3. Unreadable link: nodes 999..500 readable, then a no-access page.
    link(1000);
    put(at_(500) + 4, u32(noaccess_));
    report();
    check(num("truncated") == 1 && num("unattached") == 500 && num("cycle") == 0, "row: truncated=1 at an unreadable link");
    // Foreign head: a zero prev that is not R+0x28.
    put(at_(500) + 4, u32(manager_ + 0x800));
    report();
    check(num("truncated") == 1 && num("unattached") == 500, "row: truncated=1 on a foreign head");

    // 4. Refused counts and a missing manager.
    link(1000);
    put(tables + 0x20 + 0xc, 20000000);
    put(manager_ + 0x10, 0);
    report();
    check(field("cuts") == "-" && field("scenes") == "-" && num("engine_nodes") == 123456, "row: refused counts are -");
    put(tables + 0x20 + 0xc, 950);
    put(manager_ + 0x10, u32(tables + 0x10));
    g_manager = 0;
    report();
    check(field("engine_nodes") == "-" && num("truncated") == 1 && num("unattached") == 0 && num("tasks") == 4321 &&
              field("b0").empty(),
          "row: no manager -> counts -, walk truncated, tasks still read");
    g_manager = u32(manager_);

    // 5. 200k nodes: the row's own cost, and the walk variants, in a shuffled order (every hop a cache
    // and TLB miss) and in allocation order (the engine appends nodes at creation).
    static core::HistogramSlot table[core::histogram_slots];
    auto reader = [](std::uintptr_t a, void* out, std::size_t n) { return x3m::engine_memory::read(a, out, n); };
    auto never = [] { return false; };
    for (const bool shuffled : {true, false}) {
        link(200000, shuffled);
        double row_us[5]{};
        unsigned long long queries = 0, reads = 0, walked = 0, capped_rows = 0;
        for (auto& t : row_us) {
            report();
            t = double(num("walk_us"));
            queries = num("queries");
            reads = num("reads");
            walked = num("unattached");
            capped_rows += num("capped");
            check((num("capped") == 0 && walked == 200000 && num("sampled_nodes") == 200000) ||
                      (num("capped") == 1 && walked % core::budget_check_interval == 0 && walked < 200000),
                  "row: 200k nodes walked in full, or capped at a budget check");
        }
        std::sort(row_us, row_us + 5);
        auto time_walk = [&](unsigned stride) {
            double best = 1e30;
            for (int k = 0; k < 5; ++k) {
                x3m::engine_memory::next_frame();
                const std::uint64_t t0 = qpc();
                const core::Walk w = core::walk_unattached(reader, u32(manager_), stride, table, never);
                const double us = us_since(t0);
                if (w.length != 200000) check(false, "timed walk length");
                best = std::min(best, us);
            }
            return best;
        };
        double raw = 1e30;
        for (int k = 0; k < 5; ++k) {
            const std::uint64_t t0 = qpc();
            std::uint32_t p = 0, n = 0;
            std::memcpy(&p, manager_ + 0x30, 4);
            for (;;) {
                std::uint32_t next = 0;
                std::memcpy(&next, reinterpret_cast<void*>(std::uintptr_t(p) + 4), 4);
                if (!next) break;
                ++n;
                p = next;
            }
            raw = std::min(raw, us_since(t0));
            if (n != 200000) check(false, "raw chase length");
        }
        const double next_only = time_walk(0x7fffffffu), s64 = time_walk(64), s1 = time_walk(1);
        std::printf("SCENE GRAPH WALK layout=%s nodes=200000 raw_chase_us=%.0f next_only_us=%.0f stride64_us=%.0f "
                    "stride1_us=%.0f row_walk_us_min=%.0f row_walk_us_median=%.0f row_unattached=%llu capped_rows=%llu "
                    "row_reads=%llu row_queries=%llu\n",
                    shuffled ? "shuffled" : "allocation", raw, next_only, s64, s1, row_us[0], row_us[2], walked,
                    capped_rows, reads, queries);
    }

    // 6. The bound and the budget: 310k nodes.
    link(max_nodes);
    report();
    const bool capped = num("capped") == 1, bounded = num("bounded") == 1;
    check((bounded && num("unattached") == core::walk_bound && !capped) ||
              (capped && num("unattached") < core::walk_bound && num("unattached") % core::budget_check_interval == 0),
          "row: 310k nodes stop at the bound, or at a budget check when the budget expires first");
    x3m::engine_memory::next_frame();
    const std::uint64_t bound_t0 = qpc();
    const core::Walk bound = core::walk_unattached(reader, u32(manager_), 64, table, never);
    const double bound_us = us_since(bound_t0);
    check(bound.bounded && bound.length == core::walk_bound && !bound.truncated, "walk: the 300,000-node bound");
    std::printf("SCENE GRAPH BOUND nodes=%u row_unattached=%llu row_bounded=%u row_capped=%u row_walk_us=%llu "
                "unbudgeted_walk_us=%.0f\n",
                max_nodes, num("unattached"), unsigned(bounded), unsigned(capped), num("walk_us"), bound_us);

    // Region sizes VirtualQuery reports for the heap nodes (the reader caches 32 regions): explains queries=.
    {
        unsigned small = 0, sampled_regions = 0;
        unsigned long long total = 0;
        for (unsigned i = 0; i < 200000; i += 997) {
            MEMORY_BASIC_INFORMATION info{};
            if (VirtualQuery(nodes_[i], &info, sizeof info) != sizeof info) continue;
            ++sampled_regions;
            total += info.RegionSize;
            small += info.RegionSize <= 0x10000;
        }
        std::printf("SCENE GRAPH REGIONS sampled=%u mean_region_kb=%llu regions_le_64kb=%u\n", sampled_regions,
                    sampled_regions ? total / sampled_regions / 1024 : 0, small);
    }

    // 7. Insert-caller capture per insert (resolve + table add, the real reader, a synthetic stack).
    auto* stack = static_cast<std::uint32_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    stack[0] = core::autoid_insert_return;
    stack[6] = 0x00486d7d;
    stack[6 + 7] = 0x00401234;
    const std::uintptr_t esp0 = reinterpret_cast<std::uintptr_t>(stack);
    static core::CallerTable t{};
    auto time_inserts = [&](std::uint32_t ret) {
        constexpr unsigned n = 200000;
        t = {};
        x3m::engine_memory::next_frame();
        const std::uint64_t t0 = qpc();
        for (unsigned i = 0; i < n; ++i) {
            std::uint32_t site = 0, caller = 0;
            core::resolve_insert_caller(reader, esp0, ret, 0, core::caller_rules, core::caller_rule_count, &site, &caller);
            core::caller_add(t, site, caller);
        }
        return us_since(t0) * 1000.0 / n;
    };
    const double autoid_ns = time_inserts(core::autoid_insert_return);
    check(t.used == 1 && t.slots[0].site == 0x00486d7d && t.slots[0].caller == 0x00401234 && t.total == 200000,
          "insert callers: auto-id frame resolved through the real reader");
    const double direct_ns = time_inserts(0x00412345);
    check(t.used == 1 && t.slots[0].site == 0x00412345 && t.slots[0].caller == 0, "insert callers: unknown site, no read");
    std::printf("SCENE GRAPH INSERT autoid_ns=%.1f unknown_site_ns=%.1f\n", autoid_ns, direct_ns);

    // 8. The address_space rows (src/proxy/address_space.cpp) on this large-address-aware process: the create row's
    // single walk and the Present pass resumed across ticks; the partition invariant over span, the 16 MiB private
    // chunk class, LastError, the per-tick budget and the walk cost.
    {
        auto create_row = [&]() {
            row_[0] = 0;
            x3m::address_space::report_create(1, 2);
            return std::strncmp(row_, "address_space device=1 frame=2 when=create ", 43) == 0;
        };
        // Ticks until the pass emits; returns the tick calls and the longest tick (us, timed outside the module).
        auto present_pass = [&](unsigned& calls, double& max_tick_us) {
            calls = 0;
            max_tick_us = 0;
            row_[0] = 0;
            bool emitted = false;
            while (!emitted && calls < 1000) {
                const std::uint64_t t0 = qpc();
                emitted = x3m::address_space::tick(1, 30ull * (calls + 1));
                max_tick_us = std::max(max_tick_us, us_since(t0));
                ++calls;
            }
            return emitted && std::strncmp(row_, "address_space device=1 frame=", 29) == 0 &&
                   std::strstr(row_, " when=present ") != nullptr;
        };
        auto partition_ok = [&]() {
            return num("free_total") + num("reserved") + num("committed_private") + num("committed_mapped") +
                       num("committed_image") + num("committed_other") == num("span");
        };
        auto whole = [&]() { return num("span") == 0xFFFF0000ull && num("capped") == 0; };
        SetLastError(0x1234abcd);
        const bool shaped = create_row();
        check(shaped && GetLastError() == 0x1234abcd, "address_space: create row emitted, LastError preserved");
        check(partition_ok(), "address_space: free + reserved + committed_* == span");
        check(whole() && num("ticks") == 1, "address_space: the create walk covers 0..0xFFFEFFFF in one walk");
        check(num("total_virtual") > 0x80000000ull && num("avail_virtual") <= num("total_virtual"),
              "address_space: GlobalMemoryStatusEx fields (LAA: total_virtual above 2 GB)");
        check(num("chunk16_bytes") <= num("committed_private") && num("free_largest") <= num("free_total"),
              "address_space: chunk16_bytes <= committed_private, free_largest <= free_total");
        const unsigned long long base_count = num("chunk16_count"), base_bytes = num("chunk16_bytes"),
                                 base_big = num("big_private_count"), base_reserved = num("reserved");
        unsigned calls = 0;
        double max_tick = 0;
        SetLastError(0x2345bcde);
        const bool plain_pass = present_pass(calls, max_tick);
        check(plain_pass && GetLastError() == 0x2345bcde, "address_space: present tick emits, LastError preserved");
        check(calls == 1 && num("ticks") == 1 && whole() && partition_ok(),
              "address_space: the plain space completes in one tick");
        // Eight committed 16 MiB private regions (DXVK's host-visible chunk), one 4 MiB, one reserved 32 MiB.
        void* chunks[8]{};
        for (auto& c : chunks) c = VirtualAlloc(nullptr, 16u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        void* big = VirtualAlloc(nullptr, 4u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        void* reserve = VirtualAlloc(nullptr, 32u << 20, MEM_RESERVE, PAGE_NOACCESS);
        create_row();
        check(num("chunk16_count") == base_count + 8 && num("chunk16_bytes") == base_bytes + 8ull * (16u << 20),
              "address_space: eight 16 MiB chunks counted");
        check(num("big_private_count") == base_big + 1 && num("reserved") >= base_reserved + (32u << 20),
              "address_space: the 4 MiB region is big_private, the reservation is reserved");
        check(partition_ok() && num("chunk16_bytes") <= num("committed_private"),
              "address_space: invariants hold with the chunks");
        present_pass(calls, max_tick);
        check(num("chunk16_count") == base_count + 8 && partition_ok() && whole(),
              "address_space: the present pass counts the chunks too");
        for (auto& c : chunks) VirtualFree(c, 0, MEM_RELEASE);
        VirtualFree(big, 0, MEM_RELEASE);
        VirtualFree(reserve, 0, MEM_RELEASE);
        create_row();
        check(num("chunk16_count") == base_count, "address_space: released chunks leave the count");
        auto time_create = [&](unsigned long long& regions) {
            unsigned long long us[15];
            for (auto& u : us) {
                create_row();
                u = num("us");
            }
            regions = num("regions");
            std::sort(us, us + 15);
            return std::pair<unsigned long long, unsigned long long>(us[0], us[7]);
        };
        unsigned long long plain_regions = 0, frag_regions = 0;
        const auto plain = time_create(plain_regions);
        // Fragmented: 6,000 separate 64 KiB allocations, every other page of each PAGE_NOACCESS, so the
        // walk sees about 96,000 regions (a stand-in for a game process near the 4 GB wall).
        std::vector<void*> small;
        small.reserve(6000);
        for (unsigned i = 0; i < 6000; ++i) {
            void* p = VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!p) break;
            DWORD old = 0;
            for (unsigned page = 1; page < 16; page += 2)
                VirtualProtect(static_cast<char*>(p) + page * 0x1000, 0x1000, PAGE_NOACCESS, &old);
            small.push_back(p);
        }
        const auto frag = time_create(frag_regions);
        check(partition_ok() && whole() && frag_regions > 50000,
              "address_space: the create row walks the fragmented space whole");
        const bool frag_pass = present_pass(calls, max_tick);
        const unsigned long long pass_ticks = num("ticks"), pass_us = num("us"), pass_regions = num("regions");
        check(frag_pass && calls > 1 && pass_ticks == calls,
              "address_space: the fragmented pass takes several ticks, ticks= counts them");
        check(whole() && partition_ok() && num("chunk16_bytes") <= num("committed_private"),
              "address_space: the multi-tick row covers 0..0xFFFEFFFF and its sum holds");
        check(max_tick < 2500.0, "address_space: every tick stays near the 2 ms budget");
        for (void* p : small) VirtualFree(p, 0, MEM_RELEASE);
        std::printf("ADDRESS SPACE ROW regions=%llu us_min=%llu us_median=%llu frag_regions=%llu frag_us_min=%llu "
                    "frag_us_median=%llu pass_regions=%llu pass_ticks=%llu pass_us=%llu pass_max_tick_us=%.0f\n",
                    plain_regions, plain.first, plain.second, frag_regions, frag.first, frag.second, pass_regions,
                    pass_ticks, pass_us, max_tick);
        std::printf("ADDRESS SPACE EXAMPLE %s\n", row_);
    }

    for (auto* p : nodes_) HeapFree(GetProcessHeap(), 0, p);
    std::printf("SCENE GRAPH CENSUS CPU checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
