// Amendment 17 (capital-gate-transit.md section 17.1, review F3): the host cost per frame of what site B adds per ship call with the
// trace on: the trace slot lookup (trace_slot_of over ship_trace_max slots) and, for an untraced ship, amendment 17's capital warn
// watch with its two game-memory reads (the order word [fl + order] through the ship's flight pointer, the flags [obj + 0x40]) and,
// when any watch slot is held, the 16-slot scan. 300 ship calls per frame over 300 separate ship and flight records (the reads go
// through memory, not registers); no ship warned or on 0x12 (the common case). Host x86-64 -O2 timing, best of 5; the DLL is i686.
// Usage (from the repository root):
//   c++ -std=c++17 -O2 verification/results/ship-steering/capital-crowd/trace_scan_cost.cpp -o /tmp/trace_scan_cost && /tmp/trace_scan_cost
#include <chrono>
#include <cstdint>
#include <cstdio>
struct Slot {
    std::uint32_t obj, seen;
    std::uint8_t a, b, p[2];
};
struct Flight {
    std::uint8_t pad[0x40];
    std::uint16_t order;
    std::uint8_t pad2[0xbe];
};
struct Obj {
    std::uint8_t pad[0x40];
    std::uint32_t flags;
    Flight* fl;
    std::uint8_t pad2[0xb8];
};
static Slot trace_slots[16], cap_slots[16];
static unsigned cap_used = 16; // the DLL's cap_warn_used_: 0 = no slot held (the scan is skipped)
static Obj objs[300];
static Flight flights[300];
static volatile std::uint32_t sink;
__attribute__((noinline)) static bool call(const Obj* o, unsigned tmax, bool watch) {
    const std::uint32_t id = std::uint32_t(reinterpret_cast<std::uintptr_t>(o));
    for (unsigned i = 0; i < tmax; ++i)
        if (trace_slots[i].obj == id) return true;
    if (!watch) return false;
    const std::uint16_t order = o->fl->order;                     // the two game-memory reads of capital_warn_watch
    const bool avoid = order == 0x12, warned = !avoid && (o->flags & 0x3000) == 0x3000;
    if (!warned && !avoid && !cap_used) return false;
    unsigned k = 0;
    while (k < 16 && cap_slots[k].obj != id) ++k;
    return k < 16 || warned || avoid;
}
int main() {
    for (unsigned i = 0; i < 16; ++i) {
        trace_slots[i].obj = 0x1000 + i;
        cap_slots[i].obj = 0x2000 + i;
    }
    for (unsigned s = 0; s < 300; ++s) {
        objs[s].flags = 0x80000;
        objs[s].fl = &flights[s];
        flights[s].order = 4;
    }
    const unsigned frames = 20000, ships = 300;
    for (int variant = 0; variant < 4; ++variant) {
        const unsigned tmax = variant == 0 ? 6 : 8;
        const bool watch = variant >= 2;
        cap_used = variant == 3 ? 0u : 16u;
        double best = 1e9;
        for (int rep = 0; rep < 5; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            std::uint32_t n = 0;
            for (unsigned f = 0; f < frames; ++f)
                for (unsigned s = 0; s < ships; ++s) n += call(&objs[s], tmax, watch) ? 1u : 0u;
            sink = n;
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / frames;
            if (us < best) best = us;
        }
        std::printf("%s: %.2f us per frame for %u ship calls (best of 5)\n",
                    variant == 0 ? "6 slots (before)" : variant == 1 ? "8 slots" : variant == 2 ? "8 slots + the amendment-17 capital watch, 16 slots held"
                                                                                              : "8 slots + the amendment-17 capital watch, none held",
                    best, ships);
    }
    return 0;
}
