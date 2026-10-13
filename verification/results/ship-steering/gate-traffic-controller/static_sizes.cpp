// The gate traffic controller's static memory (gate-traffic-controller.md D.8, A.1): sizeof of the boards, the arrival records, the
// membership bitmaps and the obstacle snapshot at its stage-A capacity. Built for i686 (the DLL's target) the sizes are compile-time
// constants; the program prints them. Build: i686-w64-mingw32-g++ -std=c++17 -O2 -I src/proxy static_sizes.cpp -o static_sizes.exe
// (or the host c++: no member is a pointer or a long, the layout is the same), run on the host, or read the static_asserts.
#include "ship_gate_ctl_core.h"
#include "ship_avoidance_core.h"
#include <cstdio>

namespace gc = x3m::ship_avoidance::ctl;
namespace av = x3m::ship_avoidance::core;

int main() {
    std::printf("gc::Member %zu B, gc::Gate %zu B, gc::Board (%u gates x %u members) %zu B\n", sizeof(gc::Member), sizeof(gc::Gate), gc::kGates,
                gc::kMembers, sizeof(gc::Board));
    std::printf("gc::Arrivals %zu B, gc::Index %zu B (two: members, arrivals)\n", sizeof(gc::Arrivals), sizeof(gc::Index));
    std::printf("av::Snapshot (capacity %u) %zu B; the DLL keeps 4 (kSnapshotSlots): %zu B; at 1024 it was %zu B each\n", av::kSnapshotCapacity,
                sizeof(av::Snapshot), 4 * sizeof(av::Snapshot), sizeof(av::Snapshot) - (av::kSnapshotCapacity - 1024) * sizeof(av::Obstacle));
    std::printf("controller total %zu B; with the snapshot growth %zu B\n", sizeof(gc::Board) + sizeof(gc::Arrivals) + 2 * sizeof(gc::Index),
                sizeof(gc::Board) + sizeof(gc::Arrivals) + 2 * sizeof(gc::Index) + 4 * (av::kSnapshotCapacity - 1024) * sizeof(av::Obstacle));
    return 0;
}
