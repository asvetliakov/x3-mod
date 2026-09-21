#pragma once
#include <cstddef>
#include <cstdint>

// Bounded reads of the game's own memory from inside the process: engine
// globals in the image, render nodes and cameras, the node registry's header,
// bucket array and chain links. Shared by object_trace and object_lifetime.
//
// Every [address, address+size) span is checked against a small cache of
// VirtualQuery'd regions that are committed, readable and not guard pages,
// then copied with `rep movsb` (the
// unit is built without SSE/MMX so the read path leaves XMM state untouched).
// A cached region is trusted until the next frame (next_frame(), called by the
// motion route at begin_frame) or for at most ~100 ms without a frame advance
// (a tick sampled every 64 reads, for callers outside the route), whichever
// comes first; the first touch of a region in a frame re-queries it, so the
// syscall count is one VirtualQuery per distinct region per frame instead of
// one NtReadVirtualMemory per read (docs/verification/route-cost-run1.md).
// Validated direct reads are the only mode; the historical ReadProcessMemory
// fallback (X3M_ENGINE_READS=rpm) was removed on 2026-09-22.
//
// Invalidation policy and residual risk: the hazard is a page that was
// committed when validated and decommitted before the copy. A freed block on
// a page that stays committed only yields stale bytes, which the serial/epoch
// logic already rejects. Within a frame the pointers dereferenced here are the
// node and camera the engine is submitting on this thread, the bound registry,
// its bucket array and small chain links, and image globals; the only
// cross-thread free of one of those inside a frame is a registry rehash on
// another thread, which the lifetime observer's in-flight guard rejects before
// the read. The residual window is that check-to-read interval, and only when
// the old bucket array was large enough for the heap to decommit it.
namespace x3m::engine_memory {
// false on a null address, a wrapping span, or a span not fully inside
// committed readable memory.
bool read(std::uintptr_t address, void* out, std::size_t size);
// Advances the validation epoch: every cached region is re-queried on its
// next touch. Motion route begin_frame; fixtures around a decommit.
void next_frame();
// Drops every cached region (device Reset, fixtures).
void reset();
struct Stats {
    std::uint64_t reads = 0;      // read() calls
    std::uint64_t queries = 0;    // VirtualQuery calls (cache misses and per-frame refreshes)
    std::uint64_t rejected = 0;   // spans refused by validation
    std::uint64_t frame = 0;      // current epoch (a 32-bit counter; wraps are harmless, the 100 ms tick bound still applies)
};
Stats stats();
}
