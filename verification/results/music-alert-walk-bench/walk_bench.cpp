// Per-frame / per-node cost of music_alert_core's frame() and walk on a flat fake world, native Windows (WSL interop),
// through the production engine_memory::read (VirtualQuery region cache, spin lock, GetTickCount per read) and through a
// raw memcpy reader for the logic-only floor. Not game FPS. Build/run: see run.sh beside this file.
#include "music_alert_core.h"
#include "engine_memory.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace x3m::music_alert::core;

static unsigned char* globals = nullptr; // stands in for 0x00600000..0x0060ffff (image globals)
static inline std::uint32_t xlate(std::uint32_t at) {
    if (at >= 0x00600000u && at < 0x00610000u) return std::uint32_t(std::uintptr_t(globals)) + (at - 0x00600000u);
    return at;
}
struct ValidatedMem {
    unsigned reads = 0;
    bool read(std::uint32_t at, void* out, unsigned n) { ++reads; return x3m::engine_memory::read(xlate(at), out, n); }
    std::uint32_t image32(std::uint32_t at) { std::uint32_t v; std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(xlate(at))), 4); return v; }
    void write32(std::uint32_t at, std::uint32_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(xlate(at))), &v, 4); }
};
struct RawMem {
    unsigned reads = 0;
    bool read(std::uint32_t at, void* out, unsigned n) {
        ++reads;
        if (!at) return false;
        std::memcpy(out, reinterpret_cast<const void*>(std::uintptr_t(xlate(at))), n);
        return true;
    }
    std::uint32_t image32(std::uint32_t at) { std::uint32_t v; std::memcpy(&v, reinterpret_cast<const void*>(std::uintptr_t(xlate(at))), 4); return v; }
    void write32(std::uint32_t at, std::uint32_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(xlate(at))), &v, 4); }
};
static void p32(std::uint32_t at, std::uint32_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(xlate(at))), &v, 4); }
static void p16(std::uint32_t at, std::uint16_t v) { std::memcpy(reinterpret_cast<void*>(std::uintptr_t(xlate(at))), &v, 2); }

// World pieces in one VirtualAlloc'd heap block (objects 0x400 apart: object, flight data, render).
static unsigned char* heap = nullptr;
static std::uint32_t cursor = 0;
static std::uint32_t alloc(unsigned n) { std::uint32_t a = std::uint32_t(std::uintptr_t(heap)) + cursor; cursor += n; return a; }
static void object(std::uint32_t o, std::uint32_t next, std::uint16_t cls, std::uint16_t group, std::uint32_t data, std::uint32_t render,
                   std::uint32_t sector, std::int32_t x) {
    p32(o + obj_next, next); p16(o + obj_group, group); p32(o + obj_flags1, 0); p32(o + obj_flags2, 0); p16(o + obj_class, cls);
    p32(o + obj_data, data); p32(o + obj_parent, sector); p32(o + obj_render, render);
    p32(render + render_position, std::uint32_t(x)); p32(render + render_position + 4, 0); p32(render + render_position + 8, 0);
    p32(data + flight_friend_mask, 0); p32(data + flight_foe_mask, 0); p32(data + flight_enemy_of_enabled, 0);
    p16(data + flight_order, 0); p32(data + flight_target, 0);
}
struct World { std::uint32_t R, P, F, sector, last_missile, M, cockpit; };
// mode 0: nodes hostile-group (3), ships with no order (passive hostiles: the walk cannot stop early)
// mode 1: class-7 nodes owned (group 10) within range with an attack order on the friend F (max reads per node)
// mode 2: mode 1 with P's enemy-of flag set (each target also takes the reverse stance: the most reads per node)
static World build(unsigned n_ship, unsigned n5, unsigned n6, unsigned n10, int mode) {
    cursor = 0;
    World w{};
    w.R = alloc(0x400); std::uint32_t table = alloc(0x40), buckets = alloc(0x40), link = alloc(0x40); w.cockpit = alloc(0x400);
    std::uint32_t cfg = alloc(0x800); w.M = alloc(0x100);
    w.sector = alloc(0x400); std::uint32_t lists = alloc(32 * list_stride + 16);
    p32(registry_va, w.R); p32(w.R, table); p32(w.R + registry_committed, 0); p32(w.R + registry_level, 0); p32(w.R + registry_cockpit_handle, 5);
    p32(table, buckets); p32(table + 4, 4); for (unsigned i = 0; i < 4; ++i) p32(buckets + 4 * i, 0);
    p32(buckets + 4, link); p32(link, 0); p32(link + 4, 5); p32(link + 8, w.cockpit); p32(w.cockpit + cockpit_target, 0);
    p32(cfg_va, cfg); p32(cfg + cfg_clock, 1000); p32(cfg + cfg_step, 16); p32(cfg + cfg_seta_a, 0x10000); p32(cfg + cfg_seta_b, 10);
    p32(player_root_va, w.M); p32(w.M + root_player_ship, 0); p32(w.M + root_laser_fired, 100); p32(w.M + root_missile_fired, 7);
    p32(w.M + root_laser_hits, 0); p32(w.M + root_missile_hits, 0);
    p16(w.sector + obj_class, class_sector); p32(w.sector + sector_lists, lists);
    for (unsigned c = 0; c < 32; ++c) { std::uint32_t t = alloc(0x80); p32(lists + c * list_stride, t); p32(t, 0); }
    w.P = alloc(0x400); std::uint32_t pf = alloc(0x400), pr = alloc(0x100);
    object(w.P, 0, class_ship, player_group, pf, pr, w.sector, 0);
    p32(pf + flight_friend_mask, 1u << 4); p32(pf + flight_foe_mask, 1u << 3);
    if (mode == 2) p32(pf + flight_enemy_of_enabled, 1);
    w.F = alloc(0x400); std::uint32_t ff = alloc(0x400), fr = alloc(0x100);
    object(w.F, 0, class_ship, 4, ff, fr, w.sector, 2000000); // not in any list; the owned ships' attack target
    p32(w.M + root_player_ship, w.P);
    const unsigned counts[4] = {n5, n6, n_ship, n10};
    const unsigned classes[4] = {class_5, class_6, class_ship, class_missile};
    for (unsigned k = 0; k < 4; ++k) {
        const unsigned c = classes[k];
        std::uint32_t head = 0, prev = 0, term_node = 0;
        std::memcpy(&term_node, reinterpret_cast<void*>(std::uintptr_t(lists + c * list_stride)), 4);
        unsigned n = counts[k] + (c == class_ship ? 1 : 0); // P heads the ship list
        for (unsigned i = 0; i < n; ++i) {
            std::uint32_t o;
            if (c == class_ship && i == 0) o = w.P;
            else {
                o = alloc(0x400); std::uint32_t d = alloc(0x400), r = alloc(0x100);
                const bool owned = mode >= 1 && c == class_ship;
                object(o, 0, std::uint16_t(c), owned ? player_group : 3, d, r, w.sector, owned ? 1000000 : 4000000);
                if (owned) { p16(d + flight_order, order_attack); p32(d + flight_target, w.F); }
                if (c == class_missile) w.last_missile = o;
            }
            if (prev) p32(prev + obj_next, o); else head = o;
            prev = o;
        }
        p32(prev + obj_next, term_node);
        p32(lists + c * list_stride, head);
    }
    return w;
}
static double qpc_ns() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER t; QueryPerformanceCounter(&t); return double(t.QuadPart) * 1e9 / double(f.QuadPart); }

// scenario: 0 quiet (vanilla 0, nothing fired), 1 passive hostile in range (vanilla 1, full walk, mode 0),
// 2 owned-heavy (vanilla 1, mode 1), 3 fired at the last missile (target search, vanilla 0), 4 owned-heavy with P's
// enemy-of flag (vanilla 1, mode 2)
template <class Mem> static void run(const char* label, int scenario, unsigned frames, unsigned other_regions) {
    const int mode = scenario == 2 ? 1 : scenario == 4 ? 2 : 0;
    World w = build(1400, 200, 200, 200, mode);
    static unsigned char* dummies[32];
    for (unsigned i = 0; i < other_regions; ++i)
        if (!dummies[i]) dummies[i] = static_cast<unsigned char*>(VirtualAlloc(nullptr, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Mem m; State s; Frame f;
    std::uint32_t laser = 100;
    double total = 0, best = 1e30; unsigned visited = 0;
    const auto stats0 = x3m::engine_memory::stats();
    for (unsigned i = 0; i < frames + 50; ++i) {
        x3m::engine_memory::next_frame();
        std::uint32_t scratch;
        for (unsigned d = 0; d < other_regions; ++d) x3m::engine_memory::read(std::uintptr_t(dummies[d]), &scratch, 4); // other modules' regions
        p32(w.R + registry_level, scenario == 1 || scenario == 2 || scenario == 4 ? 1 : 0);
        if (scenario == 3) { laser += 1; p32(w.M + root_laser_fired, laser); p32(w.cockpit + cockpit_target, w.last_missile); }
        m.reads = 0;
        const double t0 = qpc_ns();
        f = frame(m, s, w.R, w.P, 20000);
        const double dt = qpc_ns() - t0;
        if (i >= 50) { total += dt; if (dt < best) best = dt; visited = f.walk.visited; }
    }
    (void)stats0;
    std::printf("%-10s scenario=%d regions_other=%u ok=%u walk=%s visited=%u reads_per_frame=%u mean_ns=%.0f min_ns=%.0f ns_per_node=%.1f ns_per_read=%.1f\n",
                label, scenario, other_regions, f.ok ? 1u : 0u, walk_status_name(f.walk.status), visited, m.reads, total / frames, best,
                visited ? (total / frames) / visited : 0.0, m.reads ? (total / frames) / m.reads : 0.0);
}
int main(int argc, char** argv) {
    const unsigned frames = argc > 1 ? unsigned(std::atoi(argv[1])) : 2000;
    globals = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x1f000000), 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    heap = static_cast<unsigned char*>(VirtualAlloc(reinterpret_cast<void*>(0x20000000), 32u << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!globals || !heap || std::uintptr_t(heap) < 0x01000000u || std::uintptr_t(globals) < 0x00610000u) { std::printf("alloc failed/low %p %p\n", (void*)globals, (void*)heap); return 1; }
    {   // one VirtualQuery (the region cache's per-frame refresh) and one cached validated read, alone
        MEMORY_BASIC_INFORMATION info{}; double t0 = qpc_ns(); const unsigned n = 20000;
        for (unsigned i = 0; i < n; ++i) VirtualQuery(heap + (i & 1023) * 64, &info, sizeof info);
        const double vq = (qpc_ns() - t0) / n;
        std::uint32_t v = 0; x3m::engine_memory::next_frame(); x3m::engine_memory::read(std::uintptr_t(heap), &v, 4);
        t0 = qpc_ns();
        for (unsigned i = 0; i < n; ++i) x3m::engine_memory::read(std::uintptr_t(heap) + (i & 1023) * 4, &v, 4);
        const double rd = (qpc_ns() - t0) / n;
        std::printf("virtualquery_ns=%.0f cached_read_ns=%.1f\n", vq, rd);
    }
    for (int sc = 0; sc < 5; ++sc) {
        run<RawMem>("raw", sc, frames, 0);
        run<ValidatedMem>("validated", sc, frames, 0);
        run<ValidatedMem>("validated", sc, frames, 20);
    }
    {   // the same VirtualQuery after the world's pages were touched (the region's committed size is recomputed)
        MEMORY_BASIC_INFORMATION info{}; const unsigned n = 2000; double t0 = qpc_ns();
        for (unsigned i = 0; i < n; ++i) VirtualQuery(heap + (i & 1023) * 64, &info, sizeof info);
        const double vq = (qpc_ns() - t0) / n;
        t0 = qpc_ns();
        for (unsigned i = 0; i < n; ++i) VirtualQuery(globals, &info, sizeof info);
        std::printf("virtualquery_touched_32mb_ns=%.0f region_mb=%lu virtualquery_64kb_ns=%.0f\n", vq,
                    (unsigned long)(info.RegionSize >> 20), (qpc_ns() - t0) / n);
    }
    return 0;
}
