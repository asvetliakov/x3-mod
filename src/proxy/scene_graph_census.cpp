#include "scene_graph_census.h"
#include "scene_graph_census_core.h"
#include "engine_memory.h"
#include "object_lifetime.h"
#include "capture.h"
#include <windows.h>
#include <cstdio>

namespace x3m::scene_graph_census {
namespace {
// Sampling stride of the model-id histogram and the walk's time budget (measured costs in
// docs/reverse-engineering/object-lifetimes.md, "Run382"): a 200k-node walk is memory-latency
// bound (16-18 ms as a bare pointer chase on the fixture), so the row stops at 1.8 ms and says
// capped=1 (unattached= is then a lower bound over the newest nodes); the model-id read adds
// 0-27% per walked node (three fixture runs), so every walked node is histogrammed (sampled=1).
constexpr unsigned sample_stride = 1;
constexpr std::uint64_t budget_us = 1800;
core::HistogramSlot histogram_[core::histogram_slots];
core::CallerTable callers_{};
bool enabled_ = false;
#ifdef X3M_SCENE_GRAPH_CENSUS_FIXTURE
std::uintptr_t manager_va_ = core::render_manager_va, vm_va_ = core::script_vm_va; // the fixture's synthetic globals
#else
constexpr std::uintptr_t manager_va_ = core::render_manager_va, vm_va_ = core::script_vm_va;
#endif

bool read(std::uintptr_t address, void* out, std::size_t size) {
    return engine_memory::read(address, out, size);
}
std::uint64_t now() {
    LARGE_INTEGER v{};
    return QueryPerformanceCounter(&v) && v.QuadPart > 0 ? std::uint64_t(v.QuadPart) : 0;
}
std::uint64_t frequency() {
    LARGE_INTEGER f{};
    return QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? std::uint64_t(f.QuadPart) : 0;
}
// "<v>" or "-" for a refused count.
void format_count(char* out, std::size_t size, const core::Count& c) {
    if (c.valid)
        std::snprintf(out, size, "%lu", static_cast<unsigned long>(c.value));
    else
        std::snprintf(out, size, "-");
}
}

#ifdef X3M_SCENE_GRAPH_CENSUS_FIXTURE
void fixture_globals(std::uintptr_t manager_va, std::uintptr_t vm_va) {
    manager_va_ = manager_va;
    vm_va_ = vm_va;
}
#endif
void initialize(bool enabled) {
    enabled_ = enabled;
    object_lifetime::set_insert_callers(enabled);
}

void report(unsigned long long device, unsigned long long frame) {
    if (!enabled_) return;
    const DWORD error = GetLastError(); // QPC and VirtualQuery may set it
    const std::uint64_t freq = frequency(), started = now();
    const engine_memory::Stats before = engine_memory::stats();
    // Regions validated earlier in the frame are re-queried on first touch.
    engine_memory::revalidate();
    auto reader = [](std::uintptr_t address, void* out, std::size_t size) { return read(address, out, size); };
    const core::Counts counts = core::read_counts(reader, manager_va_, vm_va_);
    const std::uint64_t budget_ticks = freq ? budget_us * freq / 1000000u : 0;
    auto expired = [&]() { return budget_ticks && now() - started > budget_ticks; };
    core::Walk walk{};
    if (counts.manager)
        walk = core::walk_unattached(reader, counts.manager_address, sample_stride, histogram_, expired);
    else
        walk.truncated = true;
    core::TopEntry top[core::top_count]{};
    const unsigned top_n = counts.manager ? core::histogram_top(histogram_, top) : 0;
    char bodies[core::top_count * (core::name_size + 24)];
    std::size_t at = 0;
    for (unsigned i = 0; i < top_n && at < sizeof bodies; ++i) {
        char name[core::name_size];
        core::body_name(reader, counts.manager_address, top[i].id, name);
        const int n = std::snprintf(bodies + at, sizeof bodies - at, " b%u=%s:%lu", i, name,
                                    static_cast<unsigned long>(top[i].count));
        if (n > 0) at += std::size_t(n);
    }
    if (at >= sizeof bodies) at = sizeof bodies - 1;
    bodies[at] = 0;
    char callers[core::caller_top * 40];
    at = 0;
    callers[0] = 0;
    const bool taken = object_lifetime::take_insert_callers(&callers_);
    if (taken) {
        core::caller_sort(callers_);
        for (unsigned i = 0; i < callers_.used && i < core::caller_top && at < sizeof callers; ++i) {
            const int n = std::snprintf(callers + at, sizeof callers - at, " i%u=%08lx/%08lx:%lu", i,
                                        static_cast<unsigned long>(callers_.slots[i].site),
                                        static_cast<unsigned long>(callers_.slots[i].caller),
                                        static_cast<unsigned long>(callers_.slots[i].count));
            if (n > 0) at += std::size_t(n);
        }
        if (at >= sizeof callers) at = sizeof callers - 1;
        callers[at] = 0;
    }
    const auto lifetime = object_lifetime::stats();
    char nodes[12], scenes[12], cuts[12], buckets[12], tasks[12], live[12];
    format_count(nodes, sizeof nodes, counts.nodes);
    format_count(scenes, sizeof scenes, counts.scenes);
    format_count(cuts, sizeof cuts, counts.cuts);
    format_count(buckets, sizeof buckets, counts.cut_buckets);
    format_count(tasks, sizeof tasks, counts.tasks);
    format_count(live, sizeof live, core::Count{lifetime.installed, lifetime.live});
    const engine_memory::Stats after = engine_memory::stats();
    const std::uint64_t elapsed = now() - started;
    const unsigned long long walk_us = freq ? elapsed * 1000000u / freq : 0;
    log("scene_graph_census device=%llu frame=%llu engine_nodes=%s scenes=%s cuts=%s cut_buckets=%s tasks=%s "
        "unattached=%lu truncated=%u cycle=%u bounded=%u capped=%u sampled=%u sampled_nodes=%lu distinct=%lu "
        "other=%lu%s registry_live=%s inserts=%lu insert_pairs=%lu insert_dropped=%lu%s walk_us=%llu reads=%llu "
        "queries=%llu",
        device, frame, nodes, scenes, cuts, buckets, tasks, static_cast<unsigned long>(walk.length),
        unsigned(walk.truncated), unsigned(walk.cycle), unsigned(walk.bounded), unsigned(walk.capped), sample_stride,
        static_cast<unsigned long>(walk.sampled), static_cast<unsigned long>(walk.distinct),
        static_cast<unsigned long>(walk.other), bodies, live, static_cast<unsigned long>(taken ? callers_.total : 0),
        static_cast<unsigned long>(taken ? callers_.used : 0), static_cast<unsigned long>(taken ? callers_.dropped : 0),
        callers, walk_us, static_cast<unsigned long long>(after.reads - before.reads),
        static_cast<unsigned long long>(after.queries - before.queries));
    SetLastError(error);
}
}
