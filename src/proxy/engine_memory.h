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
// motion route at begin_frame) or for at most ~100 ms (GetTickCount, sampled
// on every read), whichever comes first; the first touch of a region in a
// frame re-queries it, so the syscall count is one VirtualQuery per distinct
// region per frame instead of one NtReadVirtualMemory per read
// (docs/verification/route-cost-run1.md). Once the last next_frame() (a
// Present) is older than 250 ms (a load stall, the game's shutdown), a cached
// region is trusted only for 5 ms after its own validation; after
// begin_shutdown() every read re-validates its whole span with VirtualQuery
// before the copy and is refused when any part is not committed and readable
// (docs/reverse-engineering/object-lifetimes.md, "Exit-time engine read
// fault").
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
// A Present: advances the validation epoch (every cached region is
// re-queried on its next touch), restarts the stall bound and ends a
// begin_shutdown() signal. Only the motion route's per-Present begin_frame and
// fixtures call it.
void next_frame();
// Advances the validation epoch only: neither the stall bound nor the shutdown
// signal changes. Readers outside the Present path (sector background at
// BeginScene, the fog prefill poll inside a stall, the cull census).
void revalidate();
// Drops every cached region (device Reset, fixtures).
void reset();
// Teardown signal: until the next next_frame() no cached region is trusted.
// The lifetime observer raises it when the engine's render registry is
// destroyed (engine teardown 0x004710f0, which frees the engine object next);
// at exit no frame follows, so it holds to the end. source is a string
// literal; the first one is kept for the summary row.
void begin_shutdown(const char* source);
bool shutting_down();
struct Stats {
    std::uint64_t reads = 0;         // read() calls
    std::uint64_t queries = 0;       // VirtualQuery calls (cache misses and per-frame refreshes)
    std::uint64_t rejected = 0;      // spans refused by validation
    std::uint64_t frame = 0;         // current epoch (a 32-bit counter; wraps are harmless, the 100 ms tick bound still
                                     // applies)
    std::uint64_t stalled_reads = 0; // reads with frames stalled past the bound (5 ms region trust)
    std::uint64_t strict_reads = 0;  // reads after begin_shutdown() (a VirtualQuery each)
    std::uint64_t refused_stalled = 0;  // of rejected: frames stalled past the bound, before any shutdown signal
    std::uint64_t refused_shutdown = 0; // of rejected: after begin_shutdown()
    std::uint64_t shutdown_signals = 0; // begin_shutdown() calls
    const char* shutdown_source = nullptr;
};
Stats stats();
}
