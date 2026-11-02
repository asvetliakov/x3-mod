// FL run 18 per-frame read costs, measured natively (verification/results/fl-run18-triage/read_cost.py): the production
// src/proxy/engine_memory.cpp (built no-SSE, as in CMakeLists.txt) under the call patterns of the recogniser's scope
// read and the FL providers' engine/registry reads, before and after the run-18 mitigations. Heap blocks stand in for
// the node, camera and engine object, an image global for the engine slot; next_frame() every 441 calls (run 18's
// object_trace current() calls per frame) re-validates the regions as a Present does. Not game FPS.
// Usage: fl_run18_read_bench <iterations>
#include "../../src/proxy/engine_memory.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace em = x3m::engine_memory;
namespace {
uintptr_t engine_slot_value = 0; // image global (the engine slot)
std::atomic<unsigned long> calls{0}, scoped{0};
DWORD tls = TLS_OUT_OF_INDEXES;
struct Snapshot { // object_trace::Snapshot's layout class (zeroed per call, as current() does)
    uint32_t valid, depth;
    uint64_t session;
    uintptr_t mesh, node, camera, registry, engine;
    uint32_t node_handle, camera_handle, model, lod, flags12c, flags130, position[3], basis[9], scale[4], parent, alpha;
    uint32_t matrices[64];
};
uintptr_t node_block = 0, camera_block = 0;
// object_trace_fl::current(&s, false) before the run-18 change: node span 0x154, camera handle, engine, registry.
__attribute__((noinline)) bool current_before(Snapshot* out) {
    const DWORD error = GetLastError();
    *out = {};
    calls.fetch_add(1, std::memory_order_relaxed);
    if (!TlsGetValue(tls)) return false;
    scoped.fetch_add(1, std::memory_order_relaxed);
    out->node = node_block;
    out->camera = camera_block;
    uint32_t node[0x154 / 4]{};
    if (em::read(out->node, node, sizeof node)) {
        out->valid |= 1;
        out->flags130 = node[0x130 / 4];
        out->model = node[0x144 / 4];
        std::memcpy(out->position, node + 0xb0 / 4, 12);
    }
    if (em::read(out->camera + 0x28, &out->camera_handle, 4)) out->valid |= 2;
    if (em::read(reinterpret_cast<uintptr_t>(&engine_slot_value), &out->engine, 4) && out->engine &&
        em::read(out->engine + 0xc, &out->registry, 4) && out->registry)
        out->valid |= 4;
    SetLastError(error);
    return true;
}
// object_trace::scope_flags: the same node span, nothing else.
__attribute__((noinline)) bool scope_flags(uint32_t* flags, bool* known) {
    *flags = 0;
    *known = false;
    const DWORD error = GetLastError();
    const bool scope = TlsGetValue(tls) != nullptr;
    if (scope) {
        uint32_t node[0x154 / 4];
        if (em::read(node_block, node, sizeof node)) {
            *flags = node[0x130 / 4];
            *known = true;
        }
    }
    SetLastError(error);
    return scope;
}
// The two engine/registry reads (object_trace_fl current() tail, object_lifetime_fl read_registry).
__attribute__((noinline)) bool registry_reads(uintptr_t* registry) {
    uintptr_t engine = 0;
    return em::read(reinterpret_cast<uintptr_t>(&engine_slot_value), &engine, 4) && engine &&
           em::read(engine + 0xc, registry, 4) && *registry;
}
// The evaluated (not taken) per-frame cache hit: an epoch load, the shutdown flag, the sequence-checked loads,
// refilled on an epoch miss. The epoch is the bench's own frame counter (engine_memory has no epoch accessor).
std::atomic<uint32_t> seq{2}, c_epoch{0}, c_registry{0}, frame_epoch{1};
__attribute__((noinline)) bool registry_cached(uintptr_t* registry) {
    const uint32_t epoch = frame_epoch.load(std::memory_order_relaxed);
    if (em::shutting_down()) return registry_reads(registry);
    const uint32_t before = seq.load(std::memory_order_acquire);
    const uint32_t e = c_epoch.load(std::memory_order_relaxed);
    const uintptr_t r = c_registry.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (seq.load(std::memory_order_relaxed) != before || e != epoch) {
        if (!registry_reads(registry)) return false;
        c_epoch.store(epoch, std::memory_order_relaxed);
        c_registry.store(*registry, std::memory_order_relaxed);
        return true;
    }
    *registry = r;
    return true;
}
template <class F> double bench(unsigned iterations, F f) {
    em::reset();
    const auto t0 = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        if (i % 441 == 0) {
            em::next_frame();
            frame_epoch.fetch_add(1, std::memory_order_relaxed);
        }
        f();
    }
    return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / iterations;
}
}

int main(int argc, char** argv) {
    const unsigned iterations = argc > 1 ? unsigned(std::strtoul(argv[1], nullptr, 10)) : 2000000u;
    tls = TlsAlloc();
    TlsSetValue(tls, reinterpret_cast<void*>(1));
    auto* node = static_cast<uint32_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* camera = static_cast<uint32_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* engine = static_cast<uint32_t*>(VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    node[0x130 / 4] = 0x1; // not a jet (0x4000001 incomplete)
    camera[0x28 / 4] = 7;
    engine[0xc / 4] = reinterpret_cast<uintptr_t>(engine) + 0x100;
    engine_slot_value = reinterpret_cast<uintptr_t>(engine);
    node_block = reinterpret_cast<uintptr_t>(node);
    camera_block = reinterpret_cast<uintptr_t>(camera);
    volatile uint32_t sink = 0;
    const double before = bench(iterations, [&] { Snapshot s; current_before(&s); sink = sink + s.flags130; });
    const double after = bench(iterations, [&] { uint32_t f; bool k; scope_flags(&f, &k); sink = sink + f; });
    const double reads2 = bench(iterations, [&] { uintptr_t r = 0; registry_reads(&r); sink = sink + r; });
    const double cached = bench(iterations, [&] { uintptr_t r = 0; registry_cached(&r); sink = sink + r; });
    const double one = bench(iterations, [&] { uint32_t v = 0; em::read(camera_block + 0x28, &v, 4); sink = sink + v; });
    std::printf("BENCH iterations=%u not_jet_before_ns=%.1f not_jet_after_ns=%.1f registry_reads_ns=%.1f "
                "registry_cached_ns=%.1f one_read_ns=%.1f\n", iterations, before, after, reads2, cached, one);
    return sink == 0xffffffffu;
}
